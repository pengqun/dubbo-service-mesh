#!/bin/bash

ETCD_HOST=$(ip addr show docker0 | grep 'inet\b' | awk '{print $2}' | cut -d '/' -f 1)
if [[ -z ${ETCD_HOST} ]]; then
  ETCD_HOST=host.docker.internal
fi
ETCD_PORT=2379
ETCD_URL=${ETCD_HOST}:${ETCD_PORT}

echo ETCD_URL = $ETCD_URL

if [[ "$1" == "consumer" ]]; then
  echo "Starting consumer agent..."
  /root/dists/mesh-agent -l /root/logs/agent.log -e ${ETCD_HOST} -p 20000 -t consumer

elif [[ "$1" == "provider-small" ]]; then
  echo "Starting small provider agent..."
  /root/dists/mesh-agent -l /root/logs/agent.log -e ${ETCD_HOST} -p 30000 -d 20889 -t provider-small

elif [[ "$1" == "provider-medium" ]]; then
  echo "Starting medium provider agent..."
  /root/dists/mesh-agent -l /root/logs/agent.log -e ${ETCD_HOST} -p 30001 -d 20890 -t provider-medium

elif [[ "$1" == "provider-large" ]]; then
  echo "Starting large provider agent..."
  /root/dists/mesh-agent -l /root/logs/agent.log -e ${ETCD_HOST} -p 30002 -d 20891 -t provider-large

else
  echo "Unrecognized arguments, exit."
  exit 1
fi