# Validación de la estimación de respiración

Mediana de las ventanas de 30 s dentro del tramo de referencia. Rango 6-35 rpm, pasabanda 0.08-0.7 Hz, umbral de subarmónico 0.5, histéresis 0.05 × RMS. Referencia: conteo manual (sentado) o pacer (acostado). Entre paréntesis, el error (rpm).

| Captura | Posición | Referencia | Módulo | fase_resp / FFT | fase_resp / Autocorrelación | fase_resp / Cruces | fase_total desenvuelta / FFT | fase_total desenvuelta / Autocorrelación | fase_total desenvuelta / Cruces | fase_total integrada / FFT | fase_total integrada / Autocorrelación | fase_total integrada / Cruces |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| persona_sentada_84cm | sentado | 15.5 | 5.0 (-10.5) | 7.6 (-7.9) | 6.6 (-8.9) | 14.5 (-1.0) | 13.9 (-1.6) | 6.4 (-9.1) | 29.5 (+14.0) | 6.4 (-9.1) | 6.3 (-9.2) | 16.0 (+0.5) |
| normal_84cm | sentado | 14 | 17.0 (+3.0) | 11.8 (-2.2) | 20.4 (+6.4) | 19.7 (+5.7) | 11.3 (-2.7) | 20.0 (+6.0) | 22.2 (+8.2) | 8.5 (-5.5) | 7.2 (-6.8) | 15.2 (+1.2) |
| lenta_84cm | sentado | 8 | 3.0 (-5.0) | 10.3 (+2.3) | 10.4 (+2.4) | 16.6 (+8.6) | 6.8 (-1.2) | 6.5 (-1.5) | 11.2 (+3.2) | 6.5 (-1.5) | 7.6 (-0.4) | 16.8 (+8.8) |
| rapida_84cm | sentado | 31 | 14.0 (-17.0) | 10.7 (-20.3) | 9.8 (-21.2) | 12.8 (-18.2) | 10.9 (-20.1) | 11.2 (-19.8) | 17.4 (-13.6) | 9.8 (-21.2) | 6.8 (-24.2) | 12.6 (-18.4) |
| acostado_8rpm_50cm | acostado | 8 | 8.0 (+0.0) | 9.1 (+1.1) | 8.7 (+0.7) | 13.7 (+5.7) | 10.1 (+2.1) | 9.5 (+1.5) | 17.3 (+9.3) | 7.3 (-0.7) | 7.9 (-0.1) | 11.2 (+3.2) |
| acostado_15rpm_50cm | acostado | 15 | 16.0 (+1.0) | 12.9 (-2.1) | 11.8 (-3.2) | 15.7 (+0.7) | 6.2 (-8.8) | 7.7 (-7.3) | 11.0 (-4.0) | 6.6 (-8.4) | 6.4 (-8.6) | 12.1 (-2.9) |
| acostado_30rpm_50cm | acostado | 30 | 12.0 (-18.0) | 10.4 (-19.6) | 9.8 (-20.2) | 18.2 (-11.8) | 10.3 (-19.7) | 7.5 (-22.5) | 20.0 (-10.0) | 6.0 (-24.0) | 9.3 (-20.7) | 15.7 (-14.3) |
| **Error absoluto medio, sentado** |  |  | **8.9** | **8.2** | **9.7** | **8.4** | **6.4** | **9.1** | **9.7** | **9.3** | **10.2** | **7.3** |
| **Error absoluto medio, acostado** |  |  | **6.3** | **7.6** | **8.0** | **6.1** | **10.2** | **10.4** | **7.8** | **11.0** | **9.8** | **6.8** |
| **Error absoluto medio, todas** |  |  | **7.8** | **7.9** | **9.0** | **7.4** | **8.0** | **9.7** | **8.9** | **10.1** | **10.0** | **7.1** |

RMS mediano de la señal filtrada en el tramo:

- persona_sentada_84cm (sentado): fase_resp 0.057, fase_total desenvuelta 0.085, fase_total integrada 0.648
- normal_84cm (sentado): fase_resp 0.091, fase_total desenvuelta 0.134, fase_total integrada 0.761
- lenta_84cm (sentado): fase_resp 0.120, fase_total desenvuelta 0.796, fase_total integrada 1.379
- rapida_84cm (sentado): fase_resp 0.239, fase_total desenvuelta 2.661, fase_total integrada 4.058
- acostado_8rpm_50cm (acostado): fase_resp 0.084, fase_total desenvuelta 0.118, fase_total integrada 0.956
- acostado_15rpm_50cm (acostado): fase_resp 0.195, fase_total desenvuelta 2.264, fase_total integrada 3.327
- acostado_30rpm_50cm (acostado): fase_resp 0.065, fase_total desenvuelta 0.759, fase_total integrada 0.755

## Ritmo variable (acostado_variable_rapido_a_lento_50cm)

Pendiente de la frecuencia estimada entre los 30 s y el final (el ritmo real baja, así que debería ser claramente negativa):

| Señal | Estimador | Pendiente (rpm/min) | Mediana (rpm) |
|---|---|---|---|
| fase_resp | FFT | -1.2 | 11.3 |
| fase_resp | Autocorrelación | -0.6 | 10.4 |
| fase_resp | Cruces | -2.4 | 14.9 |
| módulo |  | -6.2 | 17.0 |
| fase_total desenvuelta | FFT | -0.6 | 7.8 |
| fase_total desenvuelta | Autocorrelación | -1.5 | 8.6 |
| fase_total desenvuelta | Cruces | -5.5 | 10.3 |
| fase_total integrada | FFT | +0.2 | 8.6 |
| fase_total integrada | Autocorrelación | +0.9 | 9.9 |
| fase_total integrada | Cruces | -3.3 | 11.2 |
