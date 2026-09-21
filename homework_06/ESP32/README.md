#  Домашня робота 5 — Обробка даних у хмарі: Rules Engine

---

## Архітектурна схема

```
┌──────────────┐
│  ESP32       │  Wokwi
│  (DHT22 sim) │
└──────┬───────┘
       │ MQTT over TLS, порт 8883
       │ топік: iot-course/ozasymenko/sensors/data
       │ payload: {"device_id","timestamp","temperature","humidity","lux"}
       ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│  AWS IoT Core (eu-north-1)                                                   │
│  ┌───────────────────────────────────────────────────────────┐               │
│  │  Rules Engine                                             │               │
│  │┌─────────────────────────┐  ┌────────────────────────────┐│               │
│  ││StoreTelemetry           │  │ TemperatureMoreThan28      ││               │
│  ││  SELECT * + timestamp() │  │  SELECT *                  ││               │
│  ││           + clientid()  │  │     WHERE temperature > 28 ││               │
│  ││           + topic(2)    │  │                            ││               │
│  │└───────────┬─────────────┘  └─────────────────────────┬──┘│               │               
│  └────────────┼──────────────────────────────────────────┼───┘               │
└───────────────┼──────────────────────────────────────────┼───────────────────┘
                │ дія dynamoDBv2                           │
                │ IAM-роль: iot_write_db                   │
                │                                          │
                ├─────────────[on error]─────┐             └───────┐         
                ▼                            │                     │
        ┌───────────────────┐              ┌─┼─────────────────────┼───────────────────┐       
        │  DynamoDB         │              │ │ ClaudWatch          │                   │
        │                   │              │ ▼                     ▼                   │
        │  iot_telemetry    │              │┌────────────────────┐┌───────────────────┐│
        │  pk: device_id    │              ││ LogGroup           ││ LogGroup          ││
        │  sk: received_at  │              ││ iot_db_store_errors││temperatureLogGroup││ 
        └───────────────────┘              │└────────────────────┘└───────────────────┘│
                                           └───────────────────────────────────────────┘
               
         
```
## Thing

![Thing](./images/Thing.png)

## Rules Engine

![Rules](./images/Rules.png)


### StoreTelemetry rule

![Store](./images/Store.png)


#### On Error
![Error](./images/Error.png)

#### TemperatureMoreThan28 rule
![Temp 28](./images/28.png)


## DynamoDB

### DB table
![DB table](./images/DB.png)

### Explore Items - Scan
![DB table](./images/table.png)

## CloudWatch
### Cloud Watch
![CW](./images/CW.png)
#### Log Streams
![LogStreams](./images/LogStreams.png)
#### Log Events
![Log events](./images/LogEvents.png)
---

