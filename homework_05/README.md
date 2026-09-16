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
│  ┌───────────────────────────────────────────────────────────┐            │
│  │  Rules Engine                                             │               │
│  │┌─────────────────────────┐  ┌────────────────────────────┐│
│  ││StoreTelemetry           │  │ TemperatureMoreThan28      ││
│  ││  SELECT * + timestamp() │  │  SELECT *                  ││
│  ││           + clientid()  │  │     WHERE temperature > 28 ││
│  ││           + topic(2)    │  │                            ││
    └──                      ─┘  └──
│  └────────────┬───────────────────┘  │
└───────────────┼──────────────────────┘
                │ дія dynamoDBv2
                │ IAM-роль: iot_rule_db_role
                │
                +─────────────[on error]─────┐                      
                ▼                            │     
        ┌───────────────────┐              ┌─+─────────────────────+───────────────────┐       
        │  DynamoDB         │              │ │                     │                   │
        │                   │              │ ▼  ClaudWatch         ▼                   │
        │  iot_telemetry    │              │┌────────────────────┐┌───────────────────┐│
        │  pk: device_id    │              ││ LogGroup           ││ LogGroup          ││
        │  sk: received_at  │              ││ iot_db_store_errors││temperatureLogGroup││ 
        └───────────────────┘              │└────────────────────┘└───────────────────┘│
                                           └───────────────────────────────────────────┘
               
         
```
## Rules Engine

### StoreTelemetry rule

### TemperatureMoreThan28 rule

## DynamoDB

## CloudWatch

---

