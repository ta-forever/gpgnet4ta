#!/bin/sh
echo "RS_LOG_LEVEL=${RS_LOG_LEVEL}"
echo "RS_DEMO_FILE_TEMPLATE=${RS_DEMO_FILE_TEMPLATE}"
echo "RS_PORT=${RS_PORT}"
echo "RS_DELAY_SECS=${RS_DELAY_SECS}"
echo "RS_MAX_SEND_RATE=${RS_MAX_SEND_RATE}"
echo ./replayserver --loglevel ${RS_LOG_LEVEL} --demofile "${RS_DEMO_FILE_TEMPLATE}" --port ${RS_PORT} --livedelaysecs ${RS_DELAY_SECS} --maxsendrate ${RS_MAX_SEND_RATE} --replayer 1
./replayserver --loglevel ${RS_LOG_LEVEL} --demofile "${RS_DEMO_FILE_TEMPLATE}" --port ${RS_PORT} --livedelaysecs ${RS_DELAY_SECS} --maxsendrate ${RS_MAX_SEND_RATE} --replayer 1
