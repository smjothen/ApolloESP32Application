#! /bin/bash
# command used to create a local cert for testing
openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365 -nodes

