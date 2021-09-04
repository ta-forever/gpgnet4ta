#!/bin/sh
echo "DC_LOG_LEVEL=${DC_LOG_LEVEL}"
echo "DC_DEMO_FILE_TEMPLATE=${DC_DEMO_FILE_TEMPLATE}"
echo "DC_PORT=${DC_PORT}"
echo ./replayserver --loglevel $DC_LOG_LEVEL --demofile "$DC_DEMO_FILE_TEMPLATE" --port $DC_PORT --compiler 1
./replayserver --loglevel $DC_LOG_LEVEL --demofile "$DC_DEMO_FILE_TEMPLATE" --port $DC_PORT --compiler 1
