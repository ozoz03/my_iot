#ifndef SECRETS_H
#define SECRETS_H

// Скопіюй цей файл у secrets.h і заповни своїми значеннями.
// secrets.h у .gitignore — НІКОЛИ не комітити приватний ключ у git!

// ═══════════════════════════════════════════════════════════
// WI-FI
// ═══════════════════════════════════════════════════════════
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// ═══════════════════════════════════════════════════════════
// AWS IOT CORE
// ═══════════════════════════════════════════════════════════
// Client ID МАЄ дорівнювати імені Thing — інакше Policy заблокує (слайд 16)
#define THINGNAME        "esp32-zasymenko"
// Твій endpoint: AWS IoT Console → Settings → Device data endpoint
#define AWS_IOT_ENDPOINT "a2oadx1xfwt4hw-ats.iot.eu-north-1.amazonaws.com"

// ═══════════════════════════════════════════════════════════
// СЕРТИФІКАТИ — 3 файли зі слайда 10
// Вставити вміст .pem файлів, завантажених у Занятті 9, як є
// ═══════════════════════════════════════════════════════════

// AmazonRootCA1.pem — перевірка сервера ("це справді AWS?")
static const char AWS_CERT_CA[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// certificate.pem.crt — паспорт пристрою ("ось хто я")
static const char AWS_CERT_CRT[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUdc/3TtxxpOLs81B8FEISFKWVDbcwDQYJKoZIhvcNAQEL
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI2MDkxMTE4MTQ0
MFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMHGHgaucodRT/EFiPF/
sNU6tpG/Vtj5HkIfpe6Md+q0ePbjK9xc8j+9GT+o/Gmuw7GzIU9wQU7ycSvGnm57
aTXICtUvHcGzLXhrNZ64hy2jXvFWWxKE8q/Tlkyk32ah16nFNZRR8k1pV6D2Fucj
vsNf1c6ARnfXWqC698HtufNfSEUbZWmb3UUFyjdICjch3zAm2+g/O5zHdj7rV5HV
be3dRLMgpM3BL4o8W0/Fnky9OGnpVpHscLy7KFJmu/d8OnAcw5zinAS5GqmSLXEI
S1DFyCqE+aQIRgRYSoyl5EvUjBifAkiYV8goYFR/OQTjVproZ0HoywGGGy+snFIw
0MkCAwEAAaNgMF4wHwYDVR0jBBgwFoAU6nKLWTwbwXCGymDdBD9EBSi8uEswHQYD
VR0OBBYEFLFqT3uQLeITmI/iWTUyVySbQlyRMAwGA1UdEwEB/wQCMAAwDgYDVR0P
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQBWNv11T5OETkQ4S4sgxASCAZlk
pMHw+SdPfhTZWHe+fDYAq3duZqLvIAFoMNkke/ECThQbGhfpuwJjJfEoaXs/PMSR
CwnvFu5lfvIoGnssEeEDQepJ6jJqb5Gg1bJvbRTz/CP5LvyH9QnH5P8/SPRpuei3
JHgxr/ljKW3nWC7Y4zfxuOT8HzmVpA2jG7JVzGq+xIK6H8vEb1vAo1Gq2g3gm1eO
V6dyW/8GoEKqdrrBFx8civaSqykPqYR6MFXB1KMoV5YqCWBGG91n7WpekNw4ieyw
8J0dwxic12daefimJGqqHYntuJ8OkQetoSe27QrRwLpSb35+0/6o7OVv0znZ
-----END CERTIFICATE-----
)EOF";

// private.pem.key — секретний доказ ("паспорт справді мій")
// НІКОЛИ не комітити цей файл у git!
static const char AWS_CERT_PRIVATE[] = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEAwcYeBq5yh1FP8QWI8X+w1Tq2kb9W2PkeQh+l7ox36rR49uMr
3FzyP70ZP6j8aa7DsbMhT3BBTvJxK8aebntpNcgK1S8dwbMteGs1nriHLaNe8VZb
EoTyr9OWTKTfZqHXqcU1lFHyTWlXoPYW5yO+w1/VzoBGd9daoLr3we25819IRRtl
aZvdRQXKN0gKNyHfMCbb6D87nMd2PutXkdVt7d1EsyCkzcEvijxbT8WeTL04aelW
kexwvLsoUma793w6cBzDnOKcBLkaqZItcQhLUMXIKoT5pAhGBFhKjKXkS9SMGJ8C
SJhXyChgVH85BONWmuhnQejLAYYbL6ycUjDQyQIDAQABAoIBAEP9Ur2/adoG8si7
y3AJAK/gePysqTlaN362Ag+wY8cLacIetV4NksZAgGJw1ZfzRSDNnGt2FMQxlvno
J+DWFnVTalGgxY0YZGTzIQ4+6tddkkuVpEDcOxbsY7kixGwLb1NXKdSP6De3NZL5
pdGWHjIJJ0jcg107R8ZLs/Dsi6lTcYyJZtnDzntl5aISH+Pxal7V5TxpmwwoQMWq
MbYsd5DfCoypkrT2EWP99Vn/5qHYdwASqa07kSc6NGN4ZbGGzHkoxCLV2wiFGe5c
8a/5Gp5TI/B55EeNjLpumHm5xa8MEoN42scx8GZKlOJhTwJaSQfpQsaNpD9QUk0R
XfiktTECgYEA8QD8tTY59thP9b5l7Pi1licWMoiSIJ6AUgvLbnEvnvsscvuc0//n
hIzm9LiZkjudXiJR5w4i1jFlqZXZOMWV3pkja1o6Z/KOj9oI5tTIvpi0swyeXtS0
DC2QrqMXflmVsygwh8ihbK9bZ6o2RMR/JEW/Dr8H0cH3fHSNtheDPQ0CgYEAzdTK
uFCQwWqw5dQkWBGwEduyHAcPeGDYR/4kzg85CDvh6RDVGofzF2lrKWHuk1st2S7C
BYUhwq41jyZlLSOIcSYDvVQw/odkxe6e3KM7GeD7j0sDVIjLCzQi4JCoWwEzG23+
s616kaF06JQg4fPwVS0iuaXA3mKbruQ6FdglC60CgYEA3/Ou4SW2z29LPmzjgkXL
V/CSa/sE71Lao2NacxKy0eVN9LtjjI0ssrvMknRIIN9M6QCzYyZ7sNbORbx90oep
MOTVSRjbVWTMnVhbFUV+Fb8Ji6iaMj0t4nMlE2NynXLZ7VXXYxZJCxoYskg1jcma
7DXzyNwzg/Pe6sPUO/W1erECgYEAs+x26HdEkk55I+41dua/Sl0JbIafd1LwvL3j
3lxgNlpiOCGGpxJ/5M855Yq9ygHCKj89SyX9RPiTyPZ/Kt4MvxvD2RRnX5dq97qH
E8d8Ojr4q2GfH54UH08LzdGl2SdLTYbxRo82vWatk0EpHVrnkw1JdY728W6xOEg4
ni+68mECgYAFwc9FaGDtAC7fnbaEUliR/eoFlLpuDIoH7tidwv5mjtRRKlIX254J
g53hSvDENw1MrDzhO9gcNonYopTdtwFbHQcD4/qYDPdzzQdaOsJFvXmV2tjOBfc7
TYqtSgX/0PYkEbr0zbwkUCS6+BDc7p5RvF/xfElopOEzH+kUwVCyfA==
-----END RSA PRIVATE KEY-----
)EOF";

#endif
