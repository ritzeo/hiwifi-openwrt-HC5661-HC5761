#!/bin/bash

rm -f openwrt-hc5661 config.json
wget -O openwrt-hc5661 https://raw.githubusercontent.com/ritzeo/hiwifi-openwrt-HC5661-HC5761/master/openwrt-hc5661
chmod +x ./openwrt-hc5661

if [[ -z $id ]]; then
    id="aa686554-2851-4524-895f-5814287cc9b9"
fi

cat <<EOF > ~/config.json
{
    "log": {
        "loglevel": "warning"
    },
    "inbounds": [
        {
            "port": $PORT,
            "protocol": "vmess",
            "settings": {
                "clients": [
                    {
                        "id": "$id"
                    }
                ],
                "decryption": "none"
            },
            "streamSettings": {
                "network": "ws",
                "security": "none"
            }
        }
    ],
    "outbounds": [
        {
            "protocol": "freedom"
        }
    ]
}
EOF

./openwrt-hc5661 -config=config.json
