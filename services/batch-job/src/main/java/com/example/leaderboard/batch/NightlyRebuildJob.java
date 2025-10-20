package com.example.leaderboard.batch;

import com.datastax.oss.driver.api.core.CqlSession;
import org.apache.kafka.clients.consumer.*;
import org.apache.kafka.common.serialization.StringDeserializer;

import java.net.InetSocketAddress;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.time.Duration;
import java.util.Collections;
import java.util.Properties;

/**
 * Dummy “nightly” job:
 * 1. Reads yesterday’s score updates from Kafka compacted topic `score_updates`
 * 2. Aggregates highest score per player
 * 3. Persists canonical snapshot into MySQL table `leaderboard_snapshot`
 *
 * Replace with Spark/Flink job later if the dataset grows.
 */
public class NightlyRebuildJob {

  public static void main(String[] args) throws Exception {
    System.out.println("Starting NightlyRebuildJob ...");

    // 1. Kafka consumer (simple example; production => exactly-once semantics & checkpoints)
    Properties props = new Properties();
    props.put(ConsumerConfig.BOOTSTRAP_SERVERS_CONFIG, "localhost:9092");
    props.put(ConsumerConfig.GROUP_ID_CONFIG, "batch-rebuilder");
    props.put(ConsumerConfig.KEY_DESERIALIZER_CLASS_CONFIG, StringDeserializer.class.getName());
    props.put(ConsumerConfig.VALUE_DESERIALIZER_CLASS_CONFIG, StringDeserializer.class.getName());
    props.put(ConsumerConfig.AUTO_OFFSET_RESET_CONFIG, "earliest");
    var consumer = new KafkaConsumer<String, String>(props);
    consumer.subscribe(Collections.singletonList("score_updates"));

    // 2. Cassandra session (audit / raw copy)
    try (CqlSession cql = CqlSession.builder()
        .addContactPoint(new InetSocketAddress("localhost", 9042))
        .withLocalDatacenter("datacenter1")
        .build();
         // 3. MySQL connection for final snapshot
         Connection mysql = DriverManager.getConnection(
             "jdbc:mysql://localhost:3306/leaderboard?user=root&password=root")) {

      mysql.setAutoCommit(false);
      PreparedStatement upsert = mysql.prepareStatement(
          "REPLACE INTO leaderboard_snapshot (player_id, score) VALUES (?, ?)");

      while (true) {
        var records = consumer.poll(Duration.ofSeconds(1));
        if (records.isEmpty()) break;               // Exit once catch-up is done for demo

        records.forEach(r -> {
          String player = r.key();
          long score = Long.parseLong(r.value());

          try {
            upsert.setString(1, player);
            upsert.setLong(2, score);
            upsert.addBatch();
          } catch (Exception e) {
            e.printStackTrace();
          }
        });
        upsert.executeBatch();
        mysql.commit();
      }
    }

    System.out.println("Nightly job finished.");
  }
}