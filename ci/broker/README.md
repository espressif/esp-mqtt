### Prerequisites
* installed docker-compose (https://docs.docker.com/compose/install/)

### Rebuild and run one-line command
```
docker-compose up -d
```

### Rebuild and run with output to console, for some debug
```
docker-compose up
```

### (Example) Successful state
```
CONTAINER ID        IMAGE                      COMMAND                  CREATED             STATUS              PORTS                      NAMES
407b9ccb85b9        eclipse-mosquitto:latest   "/docker-entrypoin..."   14 minutes ago      Up 4 seconds        0.0.0.0:42351->42351/tcp   mqtt-tcp-server
5204c0cb11ef        eclipse-mosquitto:latest   "/docker-entrypoin..."   14 minutes ago      Up 4 seconds        0.0.0.0:42352->42352/tcp   mqtt-ssl-server
cc8aa247c32e        eclipse-mosquitto:latest   "/docker-entrypoin..."   14 minutes ago      Up 4 seconds        0.0.0.0:42353->42353/tcp   mqtt-websockets-tcp-server
41e189081c47        eclipse-mosquitto:latest   "/docker-entrypoin..."   14 minutes ago      Up 4 seconds        0.0.0.0:42354->42354/tcp   mqtt-websockets-ssl-server
```
