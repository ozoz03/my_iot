import json
import os
import boto3
from dotenv import load_dotenv

load_dotenv()

TOPIC_CMD = "iot-course/ozasymenko/commands/led"
THING_NAME = os.getenv("DEVICE_ID")  # = THINGNAME на пристрої

# Регіон boto3 сам візьме з AWS_DEFAULT_REGION
iot = boto3.client("iot-data")


def send_led_command(value: str):
    payload = json.dumps(
        {"action": "set", "value": value},
        separators=(",", ":"),      # без пробілів — коротший payload
    )
    iot.publish(topic=TOPIC_CMD, qos=1, payload=payload)


def set_led_desired(value: str):
    """Записує бажаний стан у Device Shadow (desired.indicator).

    На відміну від send_led_command() — це не подія, яка губиться, поки
    пристрій спить (Deep Sleep), а стан, що лежить у хмарі й чекає.
    Пристрій сам забирає desired на кожному пробудженні через shadow/get
    і shadow/update/delta.
    """
    payload = json.dumps(
        {"state": {"desired": {"indicator": value}}},
        separators=(",", ":"),
    ).encode("utf-8")

    iot.update_thing_shadow(thingName=THING_NAME, payload=payload)