package com.eai441.controlgiro

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import org.json.JSONObject
import java.util.Locale
import java.util.UUID

class MainActivity : AppCompatActivity() {

    private lateinit var btnConectar: Button
    private lateinit var txtConexion: TextView
    private lateinit var txtDistancia: TextView
    private lateinit var txtEstadoMotor: TextView
    private lateinit var btnAutomatico: Button
    private lateinit var btnManual: Button

    private val handler = Handler(Looper.getMainLooper())
    private val bluetoothManager by lazy {
        getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    }
    private val bluetoothAdapter: BluetoothAdapter?
        get() = bluetoothManager.adapter

    private var bluetoothGatt: BluetoothGatt? = null
    private var commandCharacteristic: BluetoothGattCharacteristic? = null
    private var statusCharacteristic: BluetoothGattCharacteristic? = null

    private var conectado = false
    private var escaneando = false
    private var esperandoPermisos = false

    private val serviceUuid =
        UUID.fromString("19B10000-E8F2-537E-4F6C-D104768A1214")
    private val commandUuid =
        UUID.fromString("19B10001-E8F2-537E-4F6C-D104768A1214")
    private val statusUuid =
        UUID.fromString("19B10002-E8F2-537E-4F6C-D104768A1214")
    private val cccdUuid =
        UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    companion object {
        private const val REQUEST_BLE_PERMISSIONS = 101
        private const val SCAN_TIMEOUT_MS = 15000L
    }

    private val detenerEscaneo = Runnable {
        if (escaneando) {
            detenerBusqueda()
            if (!conectado) {
                txtConexion.text = "Arduino no encontrado. Intenta buscar de nuevo."
                btnConectar.isEnabled = true
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        btnConectar = findViewById(R.id.btnConectar)
        txtConexion = findViewById(R.id.txtConexion)
        txtDistancia = findViewById(R.id.txtDistancia)
        txtEstadoMotor = findViewById(R.id.txtEstadoMotor)
        btnAutomatico = findViewById(R.id.btnAutomatico)
        btnManual = findViewById(R.id.btnManual)

        btnConectar.text = "BUSCAR Y CONECTAR POR BLE"

        btnConectar.setOnClickListener {
            if (conectado) {
                desconectar()
            } else {
                comprobarPermisosYBuscar()
            }
        }

        findViewById<Button>(R.id.btnDerecha).setOnClickListener {
            enviarComando("R")
        }
        findViewById<Button>(R.id.btnIzquierda).setOnClickListener {
            enviarComando("L")
        }
        findViewById<Button>(R.id.btnDetener).setOnClickListener {
            enviarComando("S")
        }
        btnAutomatico.setOnClickListener {
            enviarComando("A")
        }
        btnManual.setOnClickListener {
            enviarComando("M")
        }

        txtConexion.text = "Bluetooth BLE: desconectado"
    }

    private fun permisosNecesarios(): Array<String> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    private fun comprobarPermisosYBuscar() {
        val faltantes = permisosNecesarios().filter {
            ContextCompat.checkSelfPermission(this, it) !=
                PackageManager.PERMISSION_GRANTED
        }

        if (faltantes.isNotEmpty()) {
            esperandoPermisos = true
            ActivityCompat.requestPermissions(
                this,
                faltantes.toTypedArray(),
                REQUEST_BLE_PERMISSIONS
            )
            return
        }

        buscarArduino()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(
            requestCode, permissions, grantResults
        )

        if (requestCode == REQUEST_BLE_PERMISSIONS) {
            esperandoPermisos = false

            if (grantResults.isNotEmpty() &&
                grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                buscarArduino()
            } else {
                txtConexion.text = "Se necesitan permisos Bluetooth para conectar."
            }
        }
    }

    private fun buscarArduino() {
        val adapter = bluetoothAdapter

        if (adapter == null || !adapter.isEnabled) {
            txtConexion.text = "Activa Bluetooth en el teléfono."
            return
        }

        if (escaneando) return

        val scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            txtConexion.text = "No está disponible el escáner BLE."
            return
        }

        txtConexion.text = "Buscando ControlGiro-BLE..."
        btnConectar.isEnabled = false
        escaneando = true

        val filtro = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(serviceUuid))
            .build()

        val ajustes = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        try {
            scanner.startScan(listOf(filtro), ajustes, scanCallback)
            handler.postDelayed(detenerEscaneo, SCAN_TIMEOUT_MS)
        } catch (e: SecurityException) {
            escaneando = false
            btnConectar.isEnabled = true
            txtConexion.text = "Permiso Bluetooth denegado."
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(
            callbackType: Int,
            result: android.bluetooth.le.ScanResult
        ) {
            val device = result.device

            detenerBusqueda()

            runOnUiThread {
                txtConexion.text = "Arduino encontrado. Conectando..."
            }

            try {
                bluetoothGatt = device.connectGatt(
                    this@MainActivity,
                    false,
                    gattCallback,
                    BluetoothDevice.TRANSPORT_LE
                )
            } catch (e: SecurityException) {
                runOnUiThread {
                    txtConexion.text = "No se pudo acceder al dispositivo BLE."
                    btnConectar.isEnabled = true
                }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            detenerBusqueda()
            runOnUiThread {
                txtConexion.text = "Falló la búsqueda BLE: $errorCode"
                btnConectar.isEnabled = true
            }
        }
    }

    private fun detenerBusqueda() {
        if (!escaneando) return

        escaneando = false
        handler.removeCallbacks(detenerEscaneo)

        try {
            bluetoothAdapter?.bluetoothLeScanner
                ?.stopScan(scanCallback)
        } catch (_: SecurityException) {
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(
            gatt: BluetoothGatt,
            status: Int,
            newState: Int
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                gatt.close()
                if (bluetoothGatt === gatt) bluetoothGatt = null
                conectado = false
                runOnUiThread {
                    txtConexion.text = "Falló la conexión BLE ($status)."
                    btnConectar.text = "BUSCAR Y CONECTAR POR BLE"
                    btnConectar.isEnabled = true
                }
                return
            }

            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    try {
                        gatt.discoverServices()
                    } catch (e: SecurityException) {
                        runOnUiThread {
                            txtConexion.text = "Permiso Bluetooth denegado."
                        }
                    }
                }

                BluetoothProfile.STATE_DISCONNECTED -> {
                    conectado = false
                    commandCharacteristic = null
                    statusCharacteristic = null
                    if (bluetoothGatt === gatt) bluetoothGatt = null
                    gatt.close()

                    runOnUiThread {
                        txtConexion.text = "Bluetooth desconectado."
                        btnConectar.text = "BUSCAR Y CONECTAR POR BLE"
                        btnConectar.isEnabled = true
                    }
                }
            }
        }

        override fun onServicesDiscovered(
            gatt: BluetoothGatt,
            status: Int
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                runOnUiThread {
                    txtConexion.text = "No se pudieron leer los servicios BLE."
                    btnConectar.isEnabled = true
                }
                return
            }

            val service = gatt.getService(serviceUuid)
            val command = service?.getCharacteristic(commandUuid)
            val state = service?.getCharacteristic(statusUuid)

            if (command == null || state == null) {
                runOnUiThread {
                    txtConexion.text = "El servicio BLE no coincide con ControlGiro."
                    btnConectar.isEnabled = true
                }
                gatt.disconnect()
                return
            }

            commandCharacteristic = command
            statusCharacteristic = state

            val notificationsEnabled = try {
                gatt.setCharacteristicNotification(state, true)
            } catch (_: SecurityException) {
                false
            }

            val descriptor = state.getDescriptor(cccdUuid)

            if (!notificationsEnabled || descriptor == null) {
                runOnUiThread {
                    txtConexion.text = "No se pudieron activar las notificaciones BLE."
                    btnConectar.isEnabled = true
                }
                return
            }

            try {
                val iniciado = if (Build.VERSION.SDK_INT >= 33) {
                    gatt.writeDescriptor(
                        descriptor,
                        BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    ) == BluetoothStatusCodes.SUCCESS
                } else {
                    @Suppress("DEPRECATION")
                    run {
                        descriptor.value =
                            BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        gatt.writeDescriptor(descriptor)
                    }
                }

                if (!iniciado) {
                    runOnUiThread {
                        txtConexion.text = "No se pudo configurar la notificación BLE."
                        btnConectar.isEnabled = true
                    }
                }
            } catch (_: SecurityException) {
                runOnUiThread {
                    txtConexion.text = "Permiso Bluetooth denegado."
                    btnConectar.isEnabled = true
                }
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (descriptor.uuid == cccdUuid &&
                status == BluetoothGatt.GATT_SUCCESS) {
                conectado = true

                runOnUiThread {
                    txtConexion.text = "Conectado por Bluetooth BLE"
                    btnConectar.text = "DESCONECTAR BLUETOOTH"
                    btnConectar.isEnabled = true
                }

                leerEstadoInicial(gatt)
            }
        }

        @Deprecated("Compatibilidad con Android anterior")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == statusUuid) {
                procesarEstado(String(characteristic.value, Charsets.UTF_8))
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            if (characteristic.uuid == statusUuid) {
                procesarEstado(String(value, Charsets.UTF_8))
            }
        }

        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS &&
                characteristic.uuid == statusUuid) {
                procesarEstado(
                    String(characteristic.value, Charsets.UTF_8)
                )
            }
        }
    }

    private fun leerEstadoInicial(gatt: BluetoothGatt) {
        val characteristic = statusCharacteristic ?: return

        try {
            gatt.readCharacteristic(characteristic)
        } catch (_: SecurityException) {
            // Las notificaciones continuarán actualizando el estado.
        }
    }

    private fun enviarComando(comando: String) {
        if (!conectado) {
            txtConexion.text = "Primero conecta el Arduino por BLE."
            return
        }

        val gatt = bluetoothGatt
        val characteristic = commandCharacteristic

        if (gatt == null || characteristic == null) {
            txtConexion.text = "La conexión BLE no está disponible."
            return
        }

        try {
            val bytes = comando.toByteArray(Charsets.UTF_8)

            val resultado = if (Build.VERSION.SDK_INT >= 33) {
                gatt.writeCharacteristic(
                    characteristic,
                    bytes,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                )
            } else {
                @Suppress("DEPRECATION")
                run {
                    characteristic.writeType =
                        BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                    characteristic.value = bytes
                    if (gatt.writeCharacteristic(characteristic)) {
                        BluetoothStatusCodes.SUCCESS
                    } else {
                        BluetoothStatusCodes.ERROR_UNKNOWN
                    }
                }
            }

            if (resultado != BluetoothStatusCodes.SUCCESS) {
                txtConexion.text = "No se pudo enviar el comando BLE."
            }
        } catch (_: SecurityException) {
            txtConexion.text = "Permiso Bluetooth denegado."
        }
    }

    private fun procesarEstado(contenido: String) {
        runOnUiThread {
            try {
                val json = JSONObject(contenido)
                if (json.optBoolean("success", false)) {
                    mostrarEstado(json)
                    txtConexion.text = "Conectado por Bluetooth BLE"
                }
            } catch (_: Exception) {
                // Ignorar mensajes incompletos o no JSON.
            }
        }
    }

    private fun mostrarEstado(json: JSONObject) {
        val distancia = json.optDouble("distanceCm", -1.0)
        val motor = json.optString("motorState", "DESCONOCIDO")
        val modo = json.optString("mode", "MANUAL")

        txtDistancia.text = if (distancia >= 0) {
            String.format(Locale.getDefault(), "%.1f cm", distancia)
        } else {
            "Sin lectura"
        }

        txtEstadoMotor.text = "Motor: $motor\nModo: $modo"

        if (modo == "AUTOMATICO") {
            btnAutomatico.text = "✓ MODO AUTOMÁTICO ACTIVO"
            btnManual.text = "VOLVER A MODO MANUAL"
        } else {
            btnAutomatico.text = "ACTIVAR MODO AUTOMÁTICO"
            btnManual.text = "✓ MODO MANUAL ACTIVO"
        }
    }

    private fun desconectar() {
        detenerBusqueda()
        conectado = false
        commandCharacteristic = null
        statusCharacteristic = null

        try {
            bluetoothGatt?.disconnect()
            bluetoothGatt?.close()
        } catch (_: SecurityException) {
        }

        bluetoothGatt = null
        btnConectar.text = "BUSCAR Y CONECTAR POR BLE"
        btnConectar.isEnabled = true
        txtConexion.text = "Bluetooth BLE: desconectado"
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        detenerBusqueda()

        try {
            bluetoothGatt?.disconnect()
            bluetoothGatt?.close()
        } catch (_: SecurityException) {
        }

        bluetoothGatt = null
        super.onDestroy()
    }
}
