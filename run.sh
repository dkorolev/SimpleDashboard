#!/bin/bash
#
# A simple script to cook Midichlorians logs into a cube and spawn a server to browse this cube.
# Required PipeView (`pv`) for QPS display, it can be safely commented out.

if [ $# -ge 1 ] ; then
  PORT=${2:-"3000"}
  make build build/gen_cube build/v2 && \
  time \
    (cat $1 | pv -l ;
     echo STOP ;
     curl -s localhost:$PORT/graceful_wait ;
     curl -s localhost:$PORT/c >build/c.json ;
     curl -s localhost:$PORT/graceful_shutdown) \
    | ./build/v2 --port=$PORT --enable_graceful_shutdown=true && \
  build/gen_cube --input=build/c.json --output=build/c.tsv && \
  (cd ../Current/CompactTSV ; make .current/pack) && \
  cat build/c.tsv | ../Current/CompactTSV/.current/pack >build/c.ctsv && \
  (cd ../BT ; make build build/histogram_cube_browser) && \
  SAVE_PWD=$PWD && \
  echo "http://localhost:$PORT/" && \
  (cd ../BT ; build/histogram_cube_browser --port=$PORT --input=$SAVE_PWD/build/c.ctsv --output_url_prefix="http://localhost:$PORT/")
else
  echo 'Need one parameter: The path to Midichlorians logs to process.'
  exit 1
fi
