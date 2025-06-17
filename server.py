# Impor library yang diperlukan
from flask import Flask, request, Response  # Flask untuk membuat server web API
from zeroconf import IPVersion, ServiceInfo, Zeroconf  # Zeroconf untuk mendaftarkan layanan mDNS (memudahkan penemuan server di jaringan lokal)
import requests  # Untuk mengirim HTTP request (misalnya ke ThingSpeak)
import json  # Untuk bekerja dengan data JSON
import os  # Untuk berinteraksi dengan sistem operasi (misalnya, mendapatkan path file)
import joblib  # Untuk memuat model machine learning yang sudah disimpan
import socket  # Untuk mendapatkan informasi jaringan seperti alamat IP

# Inisialisasi aplikasi Flask
app = Flask(__name__)

# Mendapatkan direktori tempat skrip Python ini berada
DIR = os.path.dirname(os.path.abspath(__file__))

# --- Konfigurasi ---
# Kunci API untuk menulis data ke ThingSpeak. Mengambil dari environment variable jika ada, jika tidak menggunakan nilai default.
THINGSPEAK_WRITE_API_KEY = os.environ.get('THINGSPEAK_WRITE_API_KEY', '1Y04VEMCGE7G4GYE')
# ID Channel ThingSpeak. Mengambil dari environment variable jika ada, jika tidak menggunakan nilai default.
THINGSPEAK_CHANNEL_ID = os.environ.get('THINGSPEAK_CHANNEL_ID', '2977596')
# Batas minimal suhu yang dianggap layak (feasible)
FEASIBLE_TEMP_MIN = 40.0
# Batas maksimal suhu yang dianggap layak (feasible)
FEASIBLE_TEMP_MAX = 70.0
# Batas maksimal kelembaban yang dianggap layak (feasible)
FEASIBLE_HUMIDITY_MAX = 25.0
# Batas minimal pH yang dianggap layak (feasible)
FEASIBLE_PH_MIN = 6.5
# Batas maksimal pH yang dianggap layak (feasible)
FEASIBLE_PH_MAX = 8.5

# Path ke file model K-Nearest Neighbors (KNN) yang sudah dilatih
MODEL_FILE = os.path.join(DIR, 'knn_model.joblib')
# Path ke file scaler yang digunakan untuk normalisasi data sebelum dimasukkan ke model
SCALER_FILE = os.path.join(DIR, 'scaler.joblib')

# --- Variabel Global ---
# Variabel untuk menyimpan model KNN yang sudah dimuat
knn_model = None
# Variabel untuk menyimpan scaler yang sudah dimuat
scaler = None

# Fungsi untuk mendapatkan alamat IP server secara otomatis (terhubung ke internet)
def get_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # Membuat socket UDP
    s.connect(("8.8.8.8", 80))  # Mencoba terhubung ke server DNS Google (untuk mendapatkan IP lokal yang digunakan untuk koneksi internet)
    ip = s.getsockname()[0]  # Mendapatkan alamat IP lokal dari socket
    s.close()  # Menutup socket
    return ip

# Fungsi untuk mendaftarkan layanan menggunakan mDNS (Multicast DNS)
def register_mdns():
    zeroconf = Zeroconf(ip_version=IPVersion.V4Only)  # Inisialisasi Zeroconf hanya untuk IPv4
    # Membuat informasi layanan yang akan didaftarkan
    service_info = ServiceInfo(
        "_http._tcp.local.",  # Jenis layanan (HTTP melalui TCP)
        "biodrying-server.local._http._tcp.local.",  # Nama layanan mDNS yang unik
        addresses=[socket.inet_aton(get_ip())],  # Alamat IP server dalam format biner
        port=5000,  # Port tempat server Flask berjalan
        properties={},  # Properti tambahan (opsional)
        server="biodrying-server.local."  # Nama host mDNS
    )
    print(f"Registering mDNS service: biodrying-server on IP {get_ip()}:5000") # Mencetak informasi pendaftaran
    zeroconf.register_service(service_info)  # Mendaftarkan layanan
    return zeroconf # Mengembalikan objek Zeroconf agar bisa di-unregister nanti

# Fungsi untuk mendapatkan alamat IP lokal server (fungsi ini mirip dengan get_ip() namun dengan penanganan error jika tidak terhubung ke internet)
def get_local_ip():
    """Mendapatkan alamat IP server secara otomatis."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) # Membuat socket UDP
    try:
        # Mencoba terhubung ke server DNS Google untuk mendapatkan IP yang relevan
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0] # Mendapatkan alamat IP
    except Exception:
        # Jika gagal (misalnya tidak ada koneksi internet), gunakan alamat IP localhost
        ip = "127.0.0.1"
    finally:
        # Pastikan socket selalu ditutup
        s.close()
    return ip

# --- Muat Model dan Scaler ---
# Fungsi untuk memuat model machine learning (KNN) dan scaler dari file
def load_model_and_scaler():
    global knn_model, scaler # Menggunakan variabel global knn_model dan scaler
    try:
        # Memuat model KNN dari file .joblib
        knn_model = joblib.load(MODEL_FILE)
        # Memuat scaler dari file .joblib
        scaler = joblib.load(SCALER_FILE)
        print(f"Model '{MODEL_FILE}' and scaler '{SCALER_FILE}' loaded successfully.") # Pesan sukses
    except Exception as e:
        # Jika terjadi error saat memuat, cetak pesan error dan set model/scaler ke None
        print(f"Error loading model/scaler: {e}")
        knn_model, scaler = None, None

# Panggil fungsi untuk memuat model dan scaler saat aplikasi dimulai
load_model_and_scaler()

# --- Endpoint API ---
# Mendefinisikan route '/biodrying_data' yang menerima request POST
@app.route('/biodrying_data', methods=['POST'])
def biodrying_data():
    """Menerima data sensor (suhu, kelembaban, pH), melakukan klasifikasi menggunakan model KNN,
    menentukan status buzzer berdasarkan aturan yang ditetapkan, dan mengirim data ke ThingSpeak."""
    global knn_model, scaler # Menggunakan variabel global knn_model dan scaler

    print(f"[Menerima Data] -> {request.data}") # Mencetak data mentah yang diterima
    try:
        # Mengurai (parse) data JSON yang diterima dari request
        data = json.loads(request.data)
        # Mengambil nilai suhu, kelembaban, dan pH dari data JSON. Jika tidak ada, gunakan nilai default 0.
        temperature = float(data.get('temperature', 0))
        humidity = float(data.get('humidity', 0))
        ph = float(data.get('ph', 0))

        # Jika model atau scaler belum berhasil dimuat, kirim respons error
        if knn_model is None or scaler is None:
            return Response(json.dumps({'error': 'Model not loaded'}), status=503, mimetype='application/json') # 503 Service Unavailable

        # Melakukan scaling (normalisasi) pada data baru menggunakan scaler yang sudah dimuat
        new_data_point_scaled = scaler.transform([[temperature, humidity, ph]])
        # Melakukan prediksi menggunakan model KNN pada data yang sudah di-scale
        prediction = int(knn_model.predict(new_data_point_scaled)[0]) # Ambil hasil prediksi pertama dan ubah ke integer
        # Memberikan label pada hasil prediksi (1 = Layak, 0 = Belum layak)
        prediction_label = "Layak" if prediction == 1 else "Belum Layak"

        print(f"Data: Temp={temperature}, Humidity={humidity}, pH={ph}") # Mencetak data sensor yang diterima
        print(f"Model Prediction: {prediction} ({prediction_label})") # Mencetak hasil prediksi model

        # --- Logika Buzzer ---
        # Komentar di bawah ini adalah logika buzzer alternatif yang berdasarkan rentang nilai sensor secara manual
        # Buzzer ON jika semua kondisi terpenuhi (siap panen)
        # is_temp_feasible = FEASIBLE_TEMP_MIN <= temperature <= FEASIBLE_TEMP_MAX
        # is_humidity_feasible = humidity <= FEASIBLE_HUMIDITY_MAX
        # is_ph_feasible = FEASIBLE_PH_MIN <= ph <= FEASIBLE_PH_MAX
        #
        # buzzer_on = is_temp_feasible and is_humidity_feasible and is_ph_feasible
        # print(f"Buzzer Conditions: Temp OK={is_temp_feasible}, Humidity OK={is_humidity_feasible}, pH OK={is_ph_feasible}")

        # Logika buzzer saat ini: Buzzer ON jika hasil prediksi model adalah 'feasible' (prediction == 1)
        buzzer_on = prediction # Jika prediction = 1 (feasible), buzzer_on = 1 (True). Jika 0 (not feasible), buzzer_on = 0 (False).
        print(f"Buzzer Status: {'ON' if buzzer_on else 'OFF'}") # Mencetak status buzzer

         # --- ThingSpeak Update ---
        # URL untuk mengirim data ke ThingSpeak
        thingspeak_url = f"https://api.thingspeak.com/update?api_key={THINGSPEAK_WRITE_API_KEY}"
        # Data (payload) yang akan dikirim ke ThingSpeak
        payload = {
            "field1": temperature,  # Data suhu untuk field1 di ThingSpeak
            "field2": humidity,  # Data kelembaban untuk field2 di ThingSpeak
            "field3": ph,  # Data pH untuk field3 di ThingSpeak
            "field4": prediction,  # Hasil prediksi untuk field4 di ThingSpeak
        }
        try:
            # Mengirim data ke ThingSpeak menggunakan metode POST
            response = requests.post(thingspeak_url, data=payload)
            response.raise_for_status() # Akan menghasilkan error jika status code HTTP adalah 4xx atau 5xx
        except requests.exceptions.RequestException as e:
            # Jika terjadi error saat mengirim ke ThingSpeak, cetak error dan kirim respons error ke client
            print(f"Error sending to ThingSpeak: {e}")
            return Response(response=json.dumps({'error': f'Failed to update ThingSpeak: {e}'}), status=500, mimetype='application/json') # 500 Internal Server Error

        # Data respons yang akan dikirim kembali ke client (ESP32/perangkat lain)
        response_data = {'classification': prediction, 'buzzer_on': buzzer_on}
        # Mengirim respons sukses dengan data klasifikasi dan status buzzer
        return Response(json.dumps(response_data), status=200, mimetype='application/json') # 200 OK

    except Exception as e:
        # Jika terjadi error lain (misalnya, data JSON tidak valid), cetak error dan kirim respons error
        print(f"Error: {e}")
        return Response(json.dumps({'error': 'Invalid data'}), status=400, mimetype='application/json') # 400 Bad Request

# Blok ini akan dieksekusi hanya jika skrip dijalankan secara langsung (bukan diimpor sebagai modul)
if __name__ == '__main__':
    zeroconf = register_mdns() # Daftarkan layanan mDNS saat server dimulai
    try:
        # Menjalankan server Flask
        # host="0.0.0.0" membuat server dapat diakses dari alamat IP manapun di jaringan
        # port=5000 adalah port yang digunakan server
        # debug=True mengaktifkan mode debug Flask (berguna saat pengembangan)
        app.run(host="0.0.0.0", port=5000, debug=True)
    except KeyboardInterrupt:
        # Jika server dihentikan dengan Ctrl+C (KeyboardInterrupt)
        print("Shutting down server...")
        zeroconf.unregister_all_services() # Batalkan pendaftaran semua layanan mDNS
        zeroconf.close() # Tutup koneksi Zeroconf