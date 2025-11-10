// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

// iot_configs.h
#ifndef IOT_CONFIGS_H
#define IOT_CONFIGS_H

// Wifi
#define IOT_CONFIG_WIFI_SSID "FCCARMEN"
#define IOT_CONFIG_WIFI_PASSWORD "1101760558velez"

// Azure IoT
#define IOT_CONFIG_IOTHUB_FQDN "ingenieriaiothub.azure-devices.net"
#define IOT_CONFIG_DEVICE_ID "Environment_Sensor_LiLyGo"
#define IOT_CONFIG_DEVICE_KEY "elTDywr9b6PmWeJuR/oPpl5a3a8siLTbjAIoTDOLAXk="
#define IOT_CONFIG_SASTOKEN "SharedAccessSignature sr=ingenieriaiothub.azure-devices.net%2Fdevices%2FEnvironment_Sensor_LiLyGo&sig=w6%2B4pVNFaCeW4jy3hFn8DfUjX02c63Ed7lPZGb66CfI%3D&se=1798740193"


// ID_Provision_Ambiental
#define ID_Provision_Ambiental 4


// Publish 1 message every 1 minute
#define DEFAULT_TELEMETRY_FREQUENCY 60

#endif // IOT_CONFIGS_H