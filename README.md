# T-Rex NVIDIA GPU miner (Ethash / Etchash / Autolykos2 / Kawpow / Blake3 / Octopus / Firopow)

## Overview

T-Rex is a versatile cryptocurrency mining software. It supports a variety of algorithms and we, as developers, are trying to do our best to make it as fast and as convenient to use as possible.

Developer fee is 1% (2% for Octopus, Autolykos2, and their dual mining modes).

## FAQ

See https://github.com/trexminer/T-Rex/wiki/FAQ

## Usage

Full list of command line options:
```
    -a, --algo                     Specify the hash algorithm to use.
                                # OhMy Miner ETC

                                High-performance Ethereum Classic (ETC) miner written in C++ and CUDA.

                                ## Quick Start

                                See docs/README for architecture overview and the full documentation index.

                                Build:

                                ```
                                mkdir build && cd build
                                cmake .. -DCMAKE_BUILD_TYPE=Release
                                make -j$(nproc)
                                ```

                                Run (example):

                                ```
                                ./ohmy-miner-etc --pool <pool_url> --wallet <wallet_address> --verbose
                                ```

                                ## Documentation

                                - Central index: docs/INDEX.md
                                - Architecture: docs/ARCHITECTURE.md
                                - Ethash: docs/ETHASH.md
                                - Stratum: docs/STRATUM.md

                                ## Contributing

                                See CONTRIBUTING.md

* **ETC-2miners**</br>
```
t-rex -a etchash -o stratum+tcp://etc.2miners.com:1010 -u 0x0924EF9ecBcC1287047cAFd2EAD3A133313eE6A2 -p x -w rig0
```

* **ETC-woolypooly**</br>
```
t-rex -a etchash -o stratum+tcp://pool.woolypooly.com:35000 -u 0x0924EF9ecBcC1287047cAFd2EAD3A133313eE6A2 -p x -w rig0
```

* **ETC-ISP-hidden-mode**</br>
```
t-rex -a etchash -o stratum+tcp://eu1-etc.ethermine.org:4444 -u 0x0924EF9ecBcC1287047cAFd2EAD3A133313eE6A2 -p x -w rig0 --no-sni --dns-https-server 1.1.1.1
```

* **ETHW-ethproxy**</br>
```
t-rex -a ethash -o stratum+http://127.0.0.1:8080
```

* **CFX-woolypooly**</br>
```
t-rex -a octopus -o stratum+tcp://pool.woolypooly.com:3094 -u cfx:aajauymfc0cpd4aj91wmfyd150avfg3fmym9j2xrh8.rig0 -p x
```

* **CFX-nanopool**</br>
```
t-rex -a octopus -o stratum+tcp://cfx-eu1.nanopool.org:17777 -u cfx:aajauymfc0cpd4aj91wmfyd150avfg3fmym9j2xrh8.rig0/your@email.org -p x
```

* **ALPH-woolypooly**</br>
```
t-rex -a blake3 -o stratum+tcp://pool.woolypooly.com:3106 -u 1qUuxVuXN2Pk4nnYTbL4qihjLWyRkVMQVYQDAajCcuPq -p x -w rig0
```

* **ALPH-herominers**</br>
```
t-rex -a blake3 -o stratum+tcp://de.alephium.herominers.com:1199 -u 1qUuxVuXN2Pk4nnYTbL4qihjLWyRkVMQVYQDAajCcuPq -p x -w rig0
```

* **RVN-2miners**</br>
```
t-rex -a kawpow -o stratum+tcp://rvn.2miners.com:6060 -u RNm4LMBGyfH8ddCGvncQKrMtxEydxwhUJL.rig -p x
```

* **RVN-ravenminer**</br>
```
t-rex -a kawpow -o stratum+tcp://stratum.ravenminer.com:3838 -u RNm4LMBGyfH8ddCGvncQKrMtxEydxwhUJL.rig -p x
```

* **RVN-woolypooly**</br>
```
t-rex -a kawpow -o stratum+tcp://pool.woolypooly.com:55555 -u RNm4LMBGyfH8ddCGvncQKrMtxEydxwhUJL.rig -p x
```

* **SERO-serocash**</br>
```
t-rex -a progpow --coin sero -o stratum+tcp://pool2.sero.cash:8808 -u JCbZnEb8XtWV814QWRpDcDxpQpXZXw4ARneAtwXNYdd3reuo4xQDcuZivopA761QnQyfMermHR9Mpi156F5n7ez9tv75Wt7vWbHXtuyZsQVWLbKNHnZgwcXbR2yZmbw89WT -p x -w rig0
```

* **VBK-reb0rn**</br>
```
t-rex -a progpow-veriblock -o stratum+tcp://vbk-reb0rn.ddns.net:8502 -u V5h6udgGe6eL4M9cYGi776WCP75URm -p x -w rig0
```

* **VEIL-woolypooly**</br>
```
t-rex -a progpow-veil -o stratum+tcp://pool.woolypooly.com:3098 -u bv1qzftz0vuqa82zy29avylv8sclskweqsrwysgrkg -p x -w rig0
```

* **ZANO-luckypool**</br>
```
t-rex -a progpowz -o stratum+tcp://zano.luckypool.io:8877 -u iZ2bZfXdeN626rkyy9YsnfeT1Qq1K6XamE4brWm3tzP5hDUAig4dHmKSqe4yyq5dgbSPjmpLbfidqPyDXAuFY2J9544F95vagSF1Xqq3eCUp -p x -w rig0
```

* **FIRO-2miners**</br>
```
t-rex -a firopow -o stratum+tcp://firo.2miners.com:8181 -u aBR3GY8eBKvEwjrVgNgSWZsteJPpFDqm6U.rig0 -p x
```

* **FIRO-mintpond**</br>
```
t-rex -a firopow -o stratum+ssl://firo.mintpond.com:3005 -u aBR3GY8eBKvEwjrVgNgSWZsteJPpFDqm6U.rig0 -p x
```

* **FIRO-woolypooly**</br>
```
t-rex -a firopow -o stratum+tcp://pool.woolypooly.com:3104 -u aBR3GY8eBKvEwjrVgNgSWZsteJPpFDqm6U.rig0 -p x
```



## JSON config file

To start T-Rex with config file `config.txt` type in the console: `t-rex -c config.txt`.
Use `config_example` file as a starting point to create your own config.</br>
If a parameter is set in the config file and also via cmd line, the latter takes precedence,
for example: `t-rex -c config.txt -w <worker_name_to_override_the_one_in_config_file>` </br>
You can also use environment variables: simply put `%YOUR_ENV_VAR%` anywhere in your config file and it will get automatically substituted with the value of `YOUR_ENV_VAR` variable at run-time.

## Watchdog

Watchdog is intended to observe miner state and restart T-Rex if it crashes or hangs for any reason.
Also, watchdog can optionally perform auto updates if a newer version is available.
We recommend using the watchdog to avoid any downtime in mining and make sure your GPUs are busy 24/7.
If you do need to disable the watchdog, you can do so using `--no-watchdog` parameter.

## HTTP API

See https://github.com/trexminer/T-Rex/wiki/API

## Antivirus alerts

In order to protect the miner from reverse engineering attacks, the binaries are packed using a third-party software which mangles the original machine code. As a result, some antivirus engines may detect certain signatures within the executable that are similar to those that real viruses protected by the same packer have. In any case, it is advisable not to use cryptocurrency miners on the computers where you store your sensitive data (wallets, passwords etc.).

## Useful links

Mining calculator: https://woolypooly.com/en/calc/what-to-mine-gpu

## Tips

In order to maximise the hashrate our software utilises all available GPU resources, so it is important that you review your overclock settings before you start mining. Our general recommendation is to start from GPU stock settings (no overclock, default power limit), and then after making sure it is stable, slowly increase your overclock to find the "sweet spot" where the miner performs at its best and still does not crash.

## Support

Discord server:
https://discord.gg/gj7jcYf

Bitcoin Talk Forum:
https://bitcointalk.org/index.php?topic=4432704.0

Official website: https://trex-miner.com
