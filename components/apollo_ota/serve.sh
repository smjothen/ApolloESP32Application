#! /bin/bash

set -e

# example:
# cd ApolloESP32Application/
# ./components/apollo_ota/serve.sh components/apollo_ota/certs ./build

origpwd=$(pwd)
certs=$origpwd/$1

echo 'using pems from :'
ls $certs

cd $2

openssl s_server -WWW -key $certs/ca_key.pem -cert $certs/ca_cert.pem -port 8070

cd $origpwd