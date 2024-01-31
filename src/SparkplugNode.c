/*
Copyright 2024 Michael Keras

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "SparkplugNode.h"



static const char* _TOPIC_NAMESPACE = "spBv1.0";
static const size_t _TOPIC_NAMESPACE_LEN = 7;


const char* _make_topic_char(const char* group_id, const char* node_id, const char* topic_type) {
    size_t group_id_len = strlen(group_id);
    size_t node_id_len = strlen(node_id);
    size_t topic_type_len = strlen(topic_type);
    size_t char_size = _TOPIC_NAMESPACE_LEN + group_id_len + node_id_len + topic_type_len + 3; // The 3 is for 3 '/' chars
    char* newChar = (char*)malloc(char_size + 1); // extra for null terminator
    if (newChar == NULL) return NULL;
    size_t pos = 0;
    memcpy(&newChar[pos], _TOPIC_NAMESPACE, _TOPIC_NAMESPACE_LEN);
    pos += _TOPIC_NAMESPACE_LEN;
    newChar[pos] = '/';
    pos += 1;
    memcpy(&newChar[pos], group_id, group_id_len);
    pos += group_id_len;
    newChar[pos] = '/';
    pos += 1;
    memcpy(&newChar[pos], topic_type, topic_type_len);
    pos += topic_type_len;
    newChar[pos] = '/';
    pos += 1;
    memcpy(&newChar[pos], node_id, node_id_len);
    newChar[char_size] = '\0'; // Set the null terminator
    return (const char*)newChar;
}

static void* _get_tag_value_address(const char* tag_name) {
    FunctionalBasicTag* tag = getTagByName(tag_name);
    if (tag == NULL) return NULL;
    return tag->value_address;
}

SparkplugNodeConfig* createSparkplugNode(const char* group_id, const char* node_id, size_t payload_buffer_size, TimestampFunction timestamp_function) {
    if (node_id == NULL || group_id == NULL || payload_buffer_size == 0 || timestamp_function == NULL) return NULL;
    SparkplugNodeConfig* newNode = (SparkplugNodeConfig*)malloc(sizeof(SparkplugNodeConfig));
    if (newNode == NULL) return NULL;

    // set timestamp function
    setBasicTagTimestampFunction(timestamp_function);

    newNode->tags_group = NULL; // NOT CURRENTLY USED
    newNode->group_id = group_id;
    newNode->node_id = node_id;
    newNode->timestamp_function = timestamp_function;
    newNode->vars.last_scan = 0;
    newNode->vars.force_scan = false;
    newNode->vars.values_changed = false;
    newNode->vars.sequence = 0;

    newNode->topics.NCMD = _make_topic_char(group_id, node_id, "NCMD");
    if (newNode->topics.NCMD == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    newNode->topics.NBIRTH = _make_topic_char(group_id, node_id, "NBIRTH");
    if (newNode->topics.NBIRTH == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    newNode->topics.NDEATH = _make_topic_char(group_id, node_id, "NDEATH");
    if (newNode->topics.NDEATH == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    newNode->topics.NDATA = _make_topic_char(group_id, node_id, "NDATA");
    if (newNode->topics.NDATA == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    
    // Allocate the BufferValue
    newNode->payload_buffer.buffer = (uint8_t*)malloc(payload_buffer_size);
    if (newNode->payload_buffer.buffer == NULL) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    newNode->payload_buffer.allocated_length = payload_buffer_size;
    newNode->payload_buffer.written_length = 0;

    // initialize sparkplug tags
    if (sparkplugInitialized()) {
        // initialize must be done by this function
        deleteSparkplugNode(newNode);
        return NULL;
    }
    if (!initializeSparkplugTags(&(newNode->payload_buffer), NULL)) {
        deleteSparkplugNode(newNode);
        return NULL;
    }
    
    newNode->node_tags.bd_seq = getBdSeqTag();
    newNode->vars.bd_seq_tag_value = (int64_t*)(newNode->node_tags.bd_seq->value_address);
    
    newNode->node_tags.scan_rate = getScanRateTag();
    newNode->vars.scan_rate_tag_value = (uint32_t*)(newNode->node_tags.scan_rate->value_address);

    newNode->node_tags.rebirth = getRebirthTag();
    newNode->vars.rebirth_tag_value = (bool*)(newNode->node_tags.rebirth->value_address);

    return newNode;
}


bool deleteSparkplugNode(SparkplugNodeConfig* sparkplug_node) {
    if (sparkplug_node == NULL) return false;

    // free the topics
    if (sparkplug_node->topics.NCMD != NULL) free(sparkplug_node->topics.NCMD);
    if (sparkplug_node->topics.NBIRTH != NULL) free(sparkplug_node->topics.NBIRTH);
    if (sparkplug_node->topics.NDEATH != NULL) free(sparkplug_node->topics.NDEATH);
    if (sparkplug_node->topics.NDATA != NULL) free(sparkplug_node->topics.NDATA);
    sparkplug_node->topics.NCMD = NULL;
    sparkplug_node->topics.NBIRTH = NULL;
    sparkplug_node->topics.NDEATH = NULL;
    sparkplug_node->topics.NDATA = NULL;

    // free the buffer
    if(sparkplug_node->payload_buffer.buffer != NULL) free(sparkplug_node->payload_buffer.buffer);
    sparkplug_node->payload_buffer.buffer = NULL;

    // delete the sparkplug tags
    deleteSparkplugTags();

    // finally, free the node itself
    free(sparkplug_node);
    return true;
}



/*
Node Functions
*/

// Flag that indecates if bdSeq should be incremented or not before making Birth/Death payload
//static bool _FLAG_BDSEQ_FOR_INCREMENT = false;

static void _increment_sequence(SparkplugNodeConfig* node) {
    if (node == NULL) return;
    node->vars.sequence += 1;
}


bool scanDue(SparkplugNodeConfig* node) {
    if (node == NULL) return false;
    if (node->vars.force_scan) {
        node->vars.force_scan = false;
        return true;
    }
    if (!(node->vars.last_scan)) return true;
    uint64_t difference = (node->timestamp_function)() - node->vars.last_scan;
    return difference >= *(node->vars.scan_rate_tag_value);
}

bool scanTags(SparkplugNodeConfig* node) {
    if (node == NULL) return false;
    node->vars.values_changed = readAllBasicTags();
    node->vars.last_scan = node->timestamp_function();
    return true;
}


/*
Sparkplug Events
*/

