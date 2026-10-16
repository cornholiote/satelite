# Configuración de red por satélite — diseño de implementación

Estado: **en preparación; no desplegar todavía**.

## Objetivo

Desde la ficha de cada satélite en Ambrosierver:

- DHCP (valor por defecto).
- IP estática: dirección IPv4, máscara, puerta de enlace, DNS primario y opcionalmente secundario.
- Mostrar la configuración real que informa el ESP32, no solo la última petición enviada.
- Aplicar cambios individualmente, con autenticación administrativa y confirmación.

## Restricción técnica

ESPHome `wifi.manual_ip` se determina en la compilación del firmware. No basta con guardar
`manual_ip` en el backend ni con añadir campos HTML. Se requiere un componente ESP-IDF
que cambie la configuración de la interfaz STA durante la ejecución y la persista en NVS.

## Protocolo propuesto

- API ESPHome nativa, servicio `set_network_config` con parámetros `mode`, `ip`,
  `netmask`, `gateway`, `dns1`, `dns2`.
- Sensor de texto de diagnóstico `network_config` con modo, IP activa y revisión de
  configuración (sin credenciales).
- El backend valida IPv4, máscara contigua, que IP/gateway pertenecen a la misma
  subred, dirección de red/broadcast y DNS.
- La UI exige el token administrativo y confirma la pérdida temporal de conexión.

## Seguridad y recuperación

1. El ESP guarda la configuración anterior como *known-good* antes de aplicar la nueva.
2. Aplica la configuración candidata y reinicia la interfaz Wi-Fi.
3. Solo confirma la candidata cuando restablece conexión con Ambrosierver y recibe
   una confirmación explícita del backend; conectarse al AP no es suficiente.
4. Si no se confirma en 120 segundos, restaura la configuración anterior; si no existe,
   vuelve a DHCP.
5. Si se cambia la subred, el backend no debe asumir que mDNS seguirá funcionando
   entre redes. Documentar cómo recuperar el equipo mediante USB.
6. No desplegar en el único satélite en uso sin una compilación validada y un
   procedimiento de recuperación.

## Pruebas de aceptación

- DHCP → estática válida → conexión y descubrimiento.
- Estática → DHCP → concesión válida.
- IP duplicada, máscara inválida, gateway fuera de subred → rechazo.
- IP estática válida pero inaccesible desde Ambrosierver → rollback.
- Reinicio eléctrico tras guardar → conserva configuración confirmada.
- Reinicio durante configuración pendiente → rollback.
- Desconexión de Wi-Fi y recuperación mediante AP cautivo.
- Compilación ESPHome y prueba OTA sobre dispositivo de ensayo.

No modificar `main` del satélite ni `audit/ambrosierver` hasta superar las pruebas.
