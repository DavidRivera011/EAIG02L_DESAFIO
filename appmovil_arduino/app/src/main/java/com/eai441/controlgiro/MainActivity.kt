package com.eai441.controlgiro

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import java.util.concurrent.Executors

class MainActivity : AppCompatActivity() {

    private lateinit var etArduinoIp: EditText
    private lateinit var btnConectar: Button
    private lateinit var txtConexion: TextView
    private lateinit var txtDistancia: TextView
    private lateinit var txtEstadoMotor: TextView

    private val executor = Executors.newSingleThreadExecutor()
    private val handler = Handler(Looper.getMainLooper())

    private val preferencias by lazy {
        getSharedPreferences("ControlGiroPrefs", MODE_PRIVATE)
    }

    @Volatile
    private var arduinoIp = ""

    @Volatile
    private var conectado = false

    private var consultando = false

    private val actualizarEstado = object : Runnable {
        override fun run() {
            if (conectado) {
                consultarEstado()
            }

            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        etArduinoIp = findViewById(R.id.etArduinoIp)
        btnConectar = findViewById(R.id.btnConectar)
        txtConexion = findViewById(R.id.txtConexion)
        txtDistancia = findViewById(R.id.txtDistancia)
        txtEstadoMotor = findViewById(R.id.txtEstadoMotor)

        // Recuperar la IP guardada anteriormente.
        val ipGuardada = preferencias.getString("arduino_ip", "") ?: ""
        etArduinoIp.setText(ipGuardada)
        arduinoIp = ipGuardada

        btnConectar.setOnClickListener {
            conectarArduino()
        }

        findViewById<Button>(R.id.btnDerecha).setOnClickListener {
            enviarComando("/cmd?value=R")
        }

        findViewById<Button>(R.id.btnIzquierda).setOnClickListener {
            enviarComando("/cmd?value=L")
        }

        findViewById<Button>(R.id.btnDetener).setOnClickListener {
            enviarComando("/cmd?value=S")
        }

        findViewById<Button>(R.id.btnAutomatico).setOnClickListener {
            enviarComando("/mode?value=A")
        }

        findViewById<Button>(R.id.btnManual).setOnClickListener {
            enviarComando("/mode?value=M")
        }

        handler.post(actualizarEstado)

        // Intentar reconectar usando la IP guardada.
        if (ipGuardada.isNotBlank()) {
            conectarArduino()
        }
    }

    private fun conectarArduino() {
        val ipIngresada = etArduinoIp.text.toString().trim()

        if (!esIpValida(ipIngresada)) {
            txtConexion.text = "Introduce una dirección IP válida"
            conectado = false
            return
        }

        arduinoIp = ipIngresada
        conectado = false
        txtConexion.text = "Conectando con el Arduino..."
        btnConectar.isEnabled = false

        executor.execute {
            try {
                val respuesta = solicitud("/status")
                val json = JSONObject(respuesta)

                if (!json.optBoolean("success", false)) {
                    throw Exception("Respuesta no válida del Arduino")
                }

                // Guardar la IP para futuros usos.
                preferencias.edit()
                    .putString("arduino_ip", ipIngresada)
                    .apply()

                conectado = true

                runOnUiThread {
                    txtConexion.text = "Conectado al Arduino: $ipIngresada"
                    btnConectar.isEnabled = true
                    mostrarEstado(json)
                }
            } catch (e: Exception) {
                conectado = false

                runOnUiThread {
                    txtConexion.text =
                        "No se pudo conectar. Revisa la IP y el Wi-Fi."
                    btnConectar.isEnabled = true
                }
            }
        }
    }

    private fun esIpValida(ip: String): Boolean {
        val patron = Regex(
            """^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"""
        )

        if (!patron.matches(ip)) return false

        return ip.split(".").all {
            val numero = it.toIntOrNull()
            numero != null && numero in 0..255
        }
    }

    private fun enviarComando(ruta: String) {
        if (!conectado || arduinoIp.isBlank()) {
            txtConexion.text = "Primero conecta el Arduino"
            return
        }

        executor.execute {
            try {
                val respuesta = solicitud(ruta)
                val json = JSONObject(respuesta)

                runOnUiThread {
                    txtConexion.text = "Conectado al Arduino"
                    mostrarEstado(json)
                }
            } catch (e: Exception) {
                conectado = false

                runOnUiThread {
                    txtConexion.text = "Conexión perdida con el Arduino"
                }
            }
        }
    }

    private fun consultarEstado() {
        if (consultando || !conectado) return

        consultando = true

        executor.execute {
            try {
                val respuesta = solicitud("/status")
                val json = JSONObject(respuesta)

                runOnUiThread {
                    mostrarEstado(json)
                    txtConexion.text = "Conectado al Arduino: $arduinoIp"
                }
            } catch (e: Exception) {
                conectado = false

                runOnUiThread {
                    txtConexion.text = "Sin conexión con el Arduino"
                }
            } finally {
                consultando = false
            }
        }
    }

    private fun solicitud(ruta: String): String {
        val conexion = URL("http://$arduinoIp$ruta")
            .openConnection() as HttpURLConnection

        try {
            conexion.requestMethod = "GET"
            conexion.connectTimeout = 3000
            conexion.readTimeout = 3000
            conexion.setRequestProperty("Connection", "close")
            conexion.useCaches = false

            if (conexion.responseCode !in 200..299) {
                throw Exception("Error HTTP: ${conexion.responseCode}")
            }

            return conexion.inputStream.bufferedReader().use {
                it.readText()
            }
        } finally {
            conexion.disconnect()
        }
    }

    private fun mostrarEstado(json: JSONObject) {
        val distancia = json.optDouble("distanceCm", -1.0)
        val motor = json.optString("motorState", "DESCONOCIDO")
        val modo = json.optString("mode", "MANUAL")

        txtDistancia.text = if (distancia >= 0) {
            String.format(
                Locale.getDefault(),
                "%.1f cm",
                distancia
            )
        } else {
            "Sin lectura"
        }

        txtEstadoMotor.text = "Motor: $motor\nModo: $modo"
    }

    override fun onDestroy() {
        handler.removeCallbacks(actualizarEstado)
        executor.shutdownNow()
        super.onDestroy()
    }
}