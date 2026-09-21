import json
import boto3
from dotenv import load_dotenv

load_dotenv()

TOPIC_CMD = "iot-course/demo/commands/led"

# Регіон boto3 сам візьме з AWS_DEFAULT_REGION
iot = boto3.client("iot-data")


def send_led_command(value: str):
    payload = json.dumps(
        {"action": "set", "value": value},
        separators=(",", ":"),      # без пробілів — коротший payload
    )
    iot.publish(topic=TOPIC_CMD, qos=1, payload=payload)