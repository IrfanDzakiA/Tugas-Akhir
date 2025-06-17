# Impor library yang diperlukan
import pandas as pd  # Pandas untuk manipulasi dan analisis data, terutama untuk membaca file CSV dan bekerja dengan DataFrame
from sklearn.neighbors import KNeighborsClassifier  # KNeighborsClassifier adalah model K-Nearest Neighbors dari scikit-learn
from sklearn.preprocessing import StandardScaler  # StandardScaler untuk melakukan penskalaan (standardisasi) fitur
from sklearn.model_selection import train_test_split, GridSearchCV  # train_test_split untuk membagi dataset, GridSearchCV untuk tuning hyperparameter
from sklearn.metrics import ( # Modul metrics untuk evaluasi model
    accuracy_score,  # Menghitung akurasi klasifikasi
    confusion_matrix,  # Menghitung confusion matrix
    classification_report,  # Membuat laporan klasifikasi (presisi, recall, f1-score)
    precision_score,  # Menghitung presisi
    recall_score,  # Menghitung recall
    f1_score,  # Menghitung F1-score
)
import joblib  # Joblib untuk menyimpan dan memuat model scikit-learn
import os  # Modul os untuk berinteraksi dengan sistem operasi (tidak secara eksplisit digunakan di sini, tapi sering ada dalam skrip ML)
from tabulate import tabulate # Tabulate untuk membuat tabel yang rapi di output konsol

# --- konfigurasi ---
TRAINING_DATA_FILE = 'Dataset20.csv'  # Nama file CSV yang berisi dataset untuk training
TEST_SIZE = 0.3  # Proporsi dataset yang akan digunakan sebagai data uji (30%)
RANDOM_STATE = 101  # Seed untuk generator angka acak, memastikan hasil pembagian data konsisten
MODEL_FILE = 'knn_model.joblib'  # Nama file untuk menyimpan model KNN yang sudah dilatih
SCALER_FILE = 'scaler.joblib'  # Nama file untuk menyimpan objek scaler
# --- parameter grid untuk GridSearchCV ---
PARAM_GRID = {
    'n_neighbors': range(1, 21),  # Daftar nilai K (jumlah tetangga) yang akan diuji, dari 1 sampai 20
}
CV = 5  # Jumlah lipatan (folds) untuk cross-validation (validasi silang 5-lipatan)


# Fungsi untuk memuat data, melakukan pra-pemrosesan, dan membaginya menjadi data latih dan data uji
def load_and_split_data():
    try:
        # Membaca data dari file CSV menggunakan pandas
        df = pd.read_csv(TRAINING_DATA_FILE)
        # Memeriksa apakah kolom yang diperlukan ('temperature', 'humidity', 'ph', 'classification') ada dalam DataFrame
        if not {'temperature', 'humidity', 'ph', 'classification'}.issubset(df.columns):
            # Jika tidak ada, tampilkan pesan error
            raise ValueError("CSV must contain 'temperature', 'humidity', 'ph', and 'classification'.")
        # Memisahkan fitur (X) dan target (y)
        X = df[['temperature', 'humidity', 'ph']].values  # Fitur: suhu, kelembaban, pH
        # Mengubah label klasifikasi dari teks ('Layak', 'Tidak Layak') menjadi numerik (1, 0)
        y = df['classification'].map({'Layak': 1, 'Tidak Layak': 0})
        # Memisahkan data menjadi data latih (train) dan data uji (test)
        # test_size: proporsi data uji
        # random_state: untuk reproduktifitas
        # stratify=y: memastikan proporsi kelas target sama di data latih dan uji
        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=TEST_SIZE, random_state=RANDOM_STATE, stratify=y
        )
        # Inisialisasi StandardScaler untuk penskalaan fitur
        scaler = StandardScaler()
        # Melakukan fit (menghitung mean dan standar deviasi) pada data latih dan mentransformasikannya
        X_train_scaled = scaler.fit_transform(X_train)
        # Mentransformasi data uji menggunakan mean dan standar deviasi dari data latih
        X_test_scaled = scaler.transform(X_test)

        # Mengembalikan data yang sudah diproses dan scaler
        return X_train_scaled, X_test_scaled, y_train, y_test, scaler
    except (FileNotFoundError, ValueError, Exception) as e:
        # Menangani error jika file tidak ditemukan, nilai salah, atau error lainnya
        print(f"Error loading/splitting data: {e}")
        return None, None, None, None, None # Mengembalikan None jika terjadi error

# Fungsi untuk melatih model KNN dan melakukan tuning hyperparameter menggunakan GridSearchCV
def train_and_tune_knn(X_train, y_train):
    """melatih knn menggunakan GridSearchCV untuk mencari parameter terbaik."""
    # Inisialisasi model KNeighborsClassifier dengan metrik jarak euclidean
    knn = KNeighborsClassifier(metric='euclidean')  # base knn classifier dengan euclidean distance
    # Inisialisasi GridSearchCV untuk mencari kombinasi parameter terbaik
    # knn: model yang akan di-tuning
    # PARAM_GRID: kamus parameter yang akan diuji
    # cv: jumlah lipatan cross-validation
    # scoring: metrik yang digunakan untuk mengevaluasi performa (akurasi)
    # verbose: level detail pesan output (2 berarti lebih detail)
    grid_search = GridSearchCV(knn, PARAM_GRID, cv=CV, scoring='accuracy', verbose=2)
    # Melatih GridSearchCV dengan data latih yang sudah di-scale
    grid_search.fit(X_train, y_train)

    # Mencetak parameter terbaik yang ditemukan oleh GridSearchCV
    print("Best parameters found:", grid_search.best_params_)
    # Mencetak skor cross-validation terbaik
    print("Best cross-validation score:", grid_search.best_score_)

    return grid_search.best_estimator_  # Mengembalikan model dengan parameter terbaik


# Fungsi untuk mengevaluasi performa model pada data uji
def evaluate_model(knn_model, X_test, y_test):
    # Melakukan prediksi pada data uji menggunakan model yang sudah dilatih
    y_pred = knn_model.predict(X_test)
    # Menghitung akurasi model
    accuracy = accuracy_score(y_test, y_pred)
    # Menghitung confusion matrix
    conf_matrix = confusion_matrix(y_test, y_pred)
    # Menghitung presisi (untuk kelas positif, secara default)
    precision = precision_score(y_test, y_pred)
    # Menghitung recall (untuk kelas positif, secara default)
    recall = recall_score(y_test, y_pred)
    # Menghitung F1-score (untuk kelas positif, secara default)
    f1 = f1_score(y_test, y_pred)
    # Membuat laporan klasifikasi yang berisi presisi, recall, f1-score untuk setiap kelas
    class_report = classification_report(y_test, y_pred)

    # Mencetak metrik evaluasi
    print(f"Accuracy: {accuracy:.4f}\n") # Akurasi dengan 4 angka di belakang koma
    print("Confusion Matrix:\n", conf_matrix) # Matriks konfusi
    print("\nClassification Report:\n", class_report) # Laporan klasifikasi

    # Menampilkan metrik evaluasi dalam bentuk tabel menggunakan tabulate
    table_data = [
        ["metrik", "nilai"], # Header tabel
        ["Accuracy", f"{accuracy:.4f}"],
        ["Precision", f"{precision:.4f}"],
        ["Recall", f"{recall:.4f}"],
        ["F1-Score", f"{f1:.4f}"],
    ]
    print("\nEvaluation Metrics Table:") # Judul tabel
    # Mencetak tabel dengan format 'grid' dan header dari baris pertama
    print(tabulate(table_data, headers="firstrow", tablefmt="grid"))

    return accuracy # Mengembalikan nilai akurasi

# Fungsi untuk menyimpan model yang sudah dilatih dan objek scaler ke file
def save_model_and_scaler(model, scaler, model_path, scaler_path):
    try:
        # Menyimpan model menggunakan joblib
        joblib.dump(model, model_path)
        # Menyimpan scaler menggunakan joblib
        joblib.dump(scaler, scaler_path)
        print(f"Model saved to {model_path}") # Pesan konfirmasi penyimpanan model
        print(f"Scaler saved to {scaler_path}") # Pesan konfirmasi penyimpanan scaler
    except Exception as e:
        # Menangani error jika gagal menyimpan model atau scaler
        print(f"Error saving model/scaler: {e}")

# Fungsi utama yang menjalankan seluruh alur proses
def main():
    # Memuat, memproses, dan membagi data. Juga mendapatkan scaler.
    X_train_scaled, X_test_scaled, y_train, y_test, scaler = load_and_split_data() # muat dan scaling data
    # Memeriksa apakah data berhasil dimuat dan diproses
    if X_train_scaled is not None and X_test_scaled is not None and scaler is not None: # Pastikan scaler juga tidak None
        # Melatih model KNN dan melakukan tuning hyperparameter
        best_knn_model = train_and_tune_knn(X_train_scaled, y_train)  # latih dan tuning model

        print("\n--- Evaluating on Test Set ---") # Header untuk bagian evaluasi
        # Mengevaluasi model terbaik pada data uji
        evaluate_model(best_knn_model, X_test_scaled, y_test)
        # Menyimpan model terbaik dan scaler yang digunakan
        save_model_and_scaler(best_knn_model, scaler, MODEL_FILE, SCALER_FILE)
    else:
        # Jika data gagal dimuat atau diproses, tampilkan pesan error
        print("Model training failed due to data loading/processing issues.")

# Blok ini akan dieksekusi hanya jika skrip dijalankan secara langsung 
if __name__ == '__main__':
    main() # Memanggil fungsi utama