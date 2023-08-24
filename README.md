# Memcached with SDRaD

This repository contains the source code of a use case for SDRaD: Memcached 

## How to get started

This repository can be cloned using the following commands:

```
git clone -b sdrad git@github.com:secure-rewind-and-discard/memcached.git
```
`sdrad` branch is based on 1.6.13. 

 - Memcached should be compiled with SDRaD library. Please make sure that Linux dynamic linker search path contains the SDRaD library path. Follow the [instructions](https://github.com/secure-rewind-and-discard/secure-rewind-and-discard). Note that Memcached is a multi-threaded application; the SDRaD library should be compiled with support `SDRAD_MULTITHREAD`. 

```
cd memcached
./autogen.sh
./configure
``` 

Compile Memcached
```
make -j4 
``` 
Run Memcached 
``` 

``` 

## Benchmark 

[YCSB](https://github.com/brianfrankcooper/YCSB) is used. 


start Memcached. Please check for different command-line arguments [here](https://github.com/memcached/memcached/wiki/ConfiguringServer#commandline-arguments). 

```
./memcached   -M -t 1  -m 30000  
```
* [Install YCSB with Memcached Binding](https://github.com/brianfrankcooper/YCSB/blob/master/memcached/README.md). Note that Python-2 is needed to run YCSB. 



Example command for YCSB. 
```
python2 ./bin/ycsb load memcached -s -P workloads/workloada -p recordcount=5000000 -p operationcount=10000000 -p "memcached.hosts=127.0.0.1" >outputload.txt

python2 ./bin/ycsb run memcached -s -P workloads/workloada -p recordcount=5000000 -p operationcount=10000000 -p "memcached.hosts=127.0.0.1" >outputload.txt
```

### Licence 
This version of Memcached is modified to support secure-rewind-and-discard

© Ericsson AB 2022-2023
  
SPDX-License-Identifier: BSD 3-Clause
 
 






 








