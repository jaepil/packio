#!/bin/bash

openssl req  -nodes -new -x509 -keyout server.key -out server.cert \
    -subj "/C=FR/ST=None/L=/O=None/OU=/CN=/emailAddress="
