# Validación de la estimación de respiración

Mediana de las ventanas de 30 s dentro del tramo del conteo. Rango 6-35 rpm, pasabanda 0.08-0.7 Hz, umbral de subarmónico 0.5, histéresis 0.05 × RMS. Entre paréntesis, el error contra el conteo (rpm).

| Captura | Conteo | Módulo | fase_resp / FFT | fase_resp / Autocorrelación | fase_resp / Cruces | fase_total desenvuelta / FFT | fase_total desenvuelta / Autocorrelación | fase_total desenvuelta / Cruces | fase_total integrada / FFT | fase_total integrada / Autocorrelación | fase_total integrada / Cruces |
|---|---|---|---|---|---|---|---|---|---|---|---|
| persona_sentada_84cm | 15.5 | 5.0 (-10.5) | 7.6 (-7.9) | 6.6 (-8.9) | 14.5 (-1.0) | 13.9 (-1.6) | 6.4 (-9.1) | 29.5 (+14.0) | 6.4 (-9.1) | 6.3 (-9.2) | 16.0 (+0.5) |
| normal_84cm | 14 | 17.0 (+3.0) | 11.8 (-2.2) | 20.4 (+6.4) | 19.7 (+5.7) | 11.3 (-2.7) | 20.0 (+6.0) | 22.2 (+8.2) | 8.5 (-5.5) | 7.2 (-6.8) | 15.2 (+1.2) |
| lenta_84cm | 8 | 3.0 (-5.0) | 10.3 (+2.3) | 10.4 (+2.4) | 16.6 (+8.6) | 6.8 (-1.2) | 6.5 (-1.5) | 11.2 (+3.2) | 6.5 (-1.5) | 7.6 (-0.4) | 16.8 (+8.8) |
| rapida_84cm | 31 | 14.0 (-17.0) | 10.7 (-20.3) | 9.8 (-21.2) | 12.8 (-18.2) | 10.9 (-20.1) | 11.2 (-19.8) | 17.4 (-13.6) | 9.8 (-21.2) | 6.8 (-24.2) | 12.6 (-18.4) |
| **Error absoluto medio** |  | **8.9** | **8.2** | **9.7** | **8.4** | **6.4** | **9.1** | **9.7** | **9.3** | **10.2** | **7.3** |

RMS mediano de la señal filtrada en el tramo:

- persona_sentada_84cm: fase_resp 0.057, fase_total desenvuelta 0.085, fase_total integrada 0.648
- normal_84cm: fase_resp 0.091, fase_total desenvuelta 0.134, fase_total integrada 0.761
- lenta_84cm: fase_resp 0.120, fase_total desenvuelta 0.796, fase_total integrada 1.379
- rapida_84cm: fase_resp 0.239, fase_total desenvuelta 2.661, fase_total integrada 4.058
