#include <BasicTag.h>
#include <SparkplugNode.h>

// Declare global variables
const char* group_id = "testing-group";  // Sparkplug group id
const char* node_id = "arduino-r4-wifi-dev";  // Sparkplug node id
size_t payload_buffer_size = 1024;  // How many bytes to allocate for storing the payloads

SparkplugNodeConfig* nodeData = NULL; 

uint64_t loop_count_var = 0;

void setup() {
  // Begin Serial connection
  Serial.begin(115200);
  delay(2000);
  Serial.println("Serial is now connected!");
  Serial.println();

  // Initialize the sparkplug node
  nodeData = createSparkplugNode(group_id, node_id, payload_buffer_size, dummyTimestampFunction);
  if (nodeData == NULL) {
    Serial.println("FAILED TO CREATE SPARKPLUG NODE");
    return;
  }

  // Change scan rate to 5 seconds
  *(nodeData->vars.scan_rate_tag_value) = 5000;

  // Print the newly instantiated node
  printSparkplugNodeConfig(nodeData);


  // create a loop count tag
  FunctionalBasicTag* loop_count_tag = createUInt64Tag("testing/loop count", &loop_count_var, getNextAlias(), false, false);
  if (loop_count_tag == NULL) {
    Serial.println("FAILED TO CREATE TAG!");
    return;
  }

  // simulate mqtt broker connection
  spnOnMQTTConnected(nodeData);
}

int scanCount = 0;
SparkplugNodeState nodeState;

void loop() {
  if (nodeData == NULL) {
    Serial.println("FAILED TO START, IGNORING LOOP!");
    delay(60000);
    return;
  }

  // increment loop count
  loop_count_var++;
  scanCount++;


  // Call the tick node function
  nodeState = tickSparkplugNode(nodeData);

  // Check the resulting SparkplugNodeState and take appropriate action
  switch(nodeState) {
    case spn_ERROR_NODE_NULL:
      scanCount -= 1;
      Serial.println("FAILED TO START, IGNORING LOOP!");
      delay(60000);
      return;
    case spn_SCAN_NOT_DUE:
      scanCount -= 1;
      return;
    case spn_SCAN_FAILED:
      scanCount -= 1;
      Serial.println("ERROR: SCAN TAGS FAILED");
      return;
    case spn_MAKE_NBIRTH_FAILED:
      Serial.println("ERROR: FAILED TO MAKE NBIRTH PAYLOAD");
      break;
    case spn_NBIRTH_PL_READY:
      Serial.println("PUBLISHING NBIRTH");
      if (dummyMQTTPublish(nodeData->mqtt_message.topic, nodeData->mqtt_message.payload->buffer, nodeData->mqtt_message.payload->written_length)) {
        spnOnPublishNBIRTH(nodeData);
      }
      break;
    case spn_VALUES_UNCHANGED:
      Serial.println("Skipping, values unchanged.");
      break;
    case spn_MAKE_NDATA_FAILED:
      Serial.println("ERROR: FAILED TO MAKE NDATA PAYLOAD");
      break;
    case spn_NDATA_PL_READY:
      Serial.println("PUBLISHING NDATA");
      if (dummyMQTTPublish(nodeData->mqtt_message.topic, nodeData->mqtt_message.payload->buffer, nodeData->mqtt_message.payload->written_length)) {
        spnOnPublishNDATA(nodeData);
      }
      break;
    default:
      scanCount -= 1;
      Serial.print("ERROR: Unknown State returned: ");
      int nodeStateInt = (int)nodeState;
      Serial.println(nodeStateInt);
      return;
  }
  
  // Periodically simulate a rebirth command
  if (scanCount == 5) {
    // reset scanCount
    scanCount = 0;
    
    *(nodeData->vars.rebirth_tag_value) = true;
  }

}


void printSparkplugNodeConfig(SparkplugNodeConfig* node) {
  // Prints relevant data from a sparkplug node
  if (node == NULL) {
    Serial.println("Provided SparkplugNodeConfig* is NULL!");
    return;
  }
  Serial.println("SparkplugNodeConfig:");

  // Print the topics
  Serial.println("Topics:");
  Serial.println(node->topics.NBIRTH);
  Serial.println(node->topics.NDEATH);
  Serial.println(node->topics.NCMD);
  Serial.println(node->topics.NDATA);
  Serial.println();

  // Print the node's tags
  Serial.print("bdSeq value: ");
  Serial.println(*(node->vars.bd_seq_tag_value));
  Serial.print("rebirth value: ");
  Serial.println(*(node->vars.rebirth_tag_value));
  Serial.print("Scan Rate value: ");
  Serial.println(*(node->vars.scan_rate_tag_value));
  Serial.println();
}

uint64_t dummyTimestampFunction() {
  /*
  This function does not provide an actual timestamp.
  A proper function would use an RTC library, sync with NTP server, etc to return a real epoch timestamp
  */
  return uint64_t(1706900000000) + millis();
}



bool dummyMQTTPublish(const char* topic, uint8_t* buffer, size_t buffer_length) {
  /* 
  A demo function which would be replaced by a real mqtt publish library/function
  */
  Serial.println();
  Serial.println("PUBLISH MQTT MESSAGE:");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  for (int i = 0; i < buffer_length; i++) {
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  Serial.println();
  return true;
}
