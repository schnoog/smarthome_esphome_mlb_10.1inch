#!/bin/bash

BT=$(grep 'ha_long_lived_token' secrets.yaml | cut -d " " -f 2- | cut -d "'" -f 2)

#echo "$BT"

# History of the entity 'sensor.temperature' of the past day (default)
NA="no_attributes=1&"
NAS=""
MR="minimal_response&"

curl \
  -H "Authorization: $BT" \
  -H "Content-Type: application/json" \
  "http://192.168.178.119:8123/api/history/period?""$NA""$MR""filter_entity_id=sensor.cyd1_temperatur_2"
