# OpenThread Border Router

Platform designs use Radio Co-Processor (RCP),

<img src="https://openthread.io/static/platforms/images/ot-arch-rcp-vert.png" width="240" align='left' />

Confirmed supported targets

- GL.iNET S200 built-in Silicon Labs EFR32MG21 module
- GL.iNET AR750 + NRF52840 USB Dongle

Manual configuration by `make menuconfig`,

```
Network  --->
	<*> ot-br-posix..................................... OpenThread Border Router  --->
		Configuration  --->
			[ ] Enable OTBR_WEB
			USB to UART Converter Driver Select (CH343)  --->
```

Or use [gl-infra-builder](https://github.com/gl-inet/gl-infra-builder) to configure directly in profile.