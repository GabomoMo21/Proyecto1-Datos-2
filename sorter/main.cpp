#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <stdexcept>

using namespace std;
using namespace std::chrono;

class PagedArray {
public:
    class Referencia {
    private:
        PagedArray& arreglo;
        int indice;

    public:
        Referencia(PagedArray& arr, int idx) : arreglo(arr), indice(idx) {} //guardar array e indice que apunta la referencia

        //Obtener valor con un indice
        Referencia& operator=(int valor) {
            arreglo.set(indice, valor);
            return *this;
        }
        //Cambiar hacer un cambio de valores a[i] = a[j]
        Referencia& operator=(const Referencia& otra) {
            int valor = (int)otra;
            arreglo.set(indice, valor);
            return *this;
        }
        //leer arr[i] como int
        operator int() const {
            return arreglo.get(indice);
        }
    };
//EStructuras para manejar la memoria
private:
    int intsPerPage;
    int pageCount;
    fstream archivo;
    int** paginas;
    int* pageNumbers;
    bool* dirtyFlags;
    int totalElementos;
    long long hits;
    long long faults;
    int siguiente;


//constructor
public:
    PagedArray(const string& filename, int pageSize, int cantidadPaginas)
        : intsPerPage(pageSize),
          pageCount(cantidadPaginas),
          paginas(nullptr),
          pageNumbers(nullptr),
          dirtyFlags(nullptr),
          totalElementos(0),
          hits(0),
          faults(0),
          siguiente(0) {

        if (intsPerPage <= 0) {
            throw invalid_argument("Pagesize tiene que ser mayor a 0");
        }

        if (pageCount <= 0) {
            throw invalid_argument("pageCount tiene que ser mayor a 0");
        }

        archivo.open(filename, ios::in | ios::out | ios::binary);
        if (!archivo.is_open()) {
            throw runtime_error("error al abrir el archivo " + filename);
        }


        //calcular cantidad de enteros
        archivo.seekg(0, ios::end);
        streampos tamanoArchivo = archivo.tellg();

        if (tamanoArchivo < 0) {
            throw runtime_error("error al obtener el tamaño del archivo");
        }

        totalElementos = (int)(tamanoArchivo / (streampos)sizeof(int));
        archivo.seekg(0, ios::beg);

        //REservar las paginas en memoria
        paginas = new int*[pageCount];
        for (int i = 0; i < pageCount; i++) {
            paginas[i] = nullptr;
        }

        try {
            for (int i = 0; i < pageCount; i++) {
                paginas[i] = new int[intsPerPage];
                for (int j = 0; j < intsPerPage; j++) {
                    paginas[i][j] = 0;
                }
            }

            pageNumbers = new int[pageCount];
            dirtyFlags = new bool[pageCount];

            for (int i = 0; i < pageCount; i++) {
                pageNumbers[i] = -1; //slot vacio
                dirtyFlags[i] = false; //ninguna pagina modificada al inicio
            }
        } catch (...) { //liberar memoria si algo falla
            if (paginas != nullptr) {
                for (int i = 0; i < pageCount; i++) {
                    delete[] paginas[i];
                }
                delete[] paginas;
                paginas = nullptr;
            }

            delete[] pageNumbers;
            pageNumbers = nullptr;

            delete[] dirtyFlags;
            dirtyFlags = nullptr;

            if (archivo.is_open()) {
                archivo.close();
            }

            throw;
        }
    }

    PagedArray(const PagedArray&) = delete;
    PagedArray& operator=(const PagedArray&) = delete;

    //destructor
    ~PagedArray() {
        try {
            flushTodasLasPaginas(); //escribir en dirtyflas antes de destruir
        } catch (...) {
        }

        if (paginas != nullptr) {
            for (int i = 0; i < pageCount; i++) {
                delete[] paginas[i];
            }
            delete[] paginas;
        }

        delete[] pageNumbers;
        delete[] dirtyFlags;

        if (archivo.is_open()) {
            archivo.close();
        }
    }
    //DEvolver referencia para read y write como un arreglo normal
    Referencia operator[](int indice) {
        return Referencia(*this, indice);
    }

    long long getHits() const {
        return hits;
    }

    long long getFaults() const {
        return faults;
    }

    int getTotalElementos() const {
        return totalElementos;
    }

    int size() const {
        return totalElementos;
    }

    int get(int indice) {
        validarIndice(indice);

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;

        int slot = asegurarPaginaCargada(pageNumber); //revisar que la pagina este cargada para leer
        return paginas[slot][offset];
    }

    void set(int indice, int valor) {
        validarIndice(indice);

        int pageNumber = indice / intsPerPage;
        int offset = indice % intsPerPage;

        int slot = asegurarPaginaCargada(pageNumber); //revisar que la pagina este cargada para escribir
        paginas[slot][offset] = valor;
        //Marcar la pagina para escriirla
        dirtyFlags[slot] = true;
    }

private:
    void validarIndice(int indice) const {
        if (indice < 0 || indice >= totalElementos) {
            throw out_of_range("Indice fuera de rango: " + to_string(indice));
        }
    }
    //busca si la pagina esta cargada
    int buscarSlotPagina(int pageNumber) const {
        for (int i = 0; i < pageCount; i++) {
            if (pageNumbers[i] == pageNumber) {
                return i;
            }
        }
        return -1;
    }
    //busca espacio en memoria
    int buscarSlotLibre() const {
        for (int i = 0; i < pageCount; i++) {
            if (pageNumbers[i] == -1) {
                return i;
            }
        }
        return -1;
    }

    //Calcula enteros por pagina. Ultima pagina puede no estar completa
    int elementosValidosEnPagina(int pageNumber) const {
        int inicio = pageNumber * intsPerPage;

        if (inicio >= totalElementos) {
            return 0;
        }

        int restantes = totalElementos - inicio;

        if (restantes >= intsPerPage) {
            return intsPerPage;
        }

        return restantes;
    }

    //escribir pagina solo si fue modificada
    void escribirPaginaSiEsNecesario(int slot) {
        if (slot < 0 || slot >= pageCount) {
            throw out_of_range("slot invalido");
        }

        if (pageNumbers[slot] == -1 || !dirtyFlags[slot]) {
            return;
        }

        int pageNumber = pageNumbers[slot];
        int elementosAEscribir = elementosValidosEnPagina(pageNumber);
        streampos pageStartByte = (streampos)pageNumber * intsPerPage * (streampos)sizeof(int);

        archivo.clear();
        archivo.seekp(pageStartByte, ios::beg);

        if (!archivo) {
            throw runtime_error("Error en seekp al escribir página");
        }

        archivo.write(reinterpret_cast<const char*>(paginas[slot]),
                      (streamsize)(elementosAEscribir * (int)sizeof(int)));

        if (!archivo) {
            throw runtime_error("Error al escribir bloque de página");
        }

        dirtyFlags[slot] = false;
    }

    //cargar pagina desde el archivo al slot
    void cargarPagina(int pageNumber, int slot) {
        if (slot < 0 || slot >= pageCount) {
            throw out_of_range("Slot inválido al cargar página");
        }

        int elementosALeer = elementosValidosEnPagina(pageNumber);
        streampos pageStartByte = (streampos)pageNumber * intsPerPage * (streampos)sizeof(int);

        archivo.clear();
        archivo.seekg(pageStartByte, ios::beg);

        if (!archivo) {
            throw runtime_error("Error en seekg al cargar página");
        }

        if (elementosALeer > 0) {
            archivo.read(reinterpret_cast<char*>(paginas[slot]),
                         (streamsize)(elementosALeer * (int)sizeof(int)));

            streamsize bytesEsperados = (streamsize)(elementosALeer * (int)sizeof(int));
            if (archivo.gcount() != bytesEsperados) {
                throw runtime_error("Error al leer bloque");
            }
        }

        //REllenar con 0 lo que no son datos reales
        for (int i = elementosALeer; i < intsPerPage; i++) {
            paginas[slot][i] = 0;
        }

        archivo.clear();
        pageNumbers[slot] = pageNumber;
        dirtyFlags[slot] = false;
    }


    // Devuelve el slot donde esta cargada la pagina y si no esta REmplazar utilizando FIFO
    int asegurarPaginaCargada(int pageNumber) {
        int slotExistente = buscarSlotPagina(pageNumber);
        if (slotExistente != -1) {
            hits++;
            return slotExistente;
        }

        faults++;

        int slotLibre = buscarSlotLibre();
        if (slotLibre != -1) {
            cargarPagina(pageNumber, slotLibre);
            return slotLibre;
        }

        int slotReemplazo = siguiente;
        escribirPaginaSiEsNecesario(slotReemplazo);
        cargarPagina(pageNumber, slotReemplazo);
        siguiente = (siguiente + 1) % pageCount;

        return slotReemplazo;
    }

    //Escrbibe las paginas pendientes antes de cerrar el archivo
    void flushTodasLasPaginas() {
        if (paginas == nullptr || pageNumbers == nullptr || dirtyFlags == nullptr) {
            return;
        }

        for (int i = 0; i < pageCount; i++) {
            escribirPaginaSiEsNecesario(i);
        }

        archivo.flush();
        if (!archivo) {
            throw runtime_error("Error al hacer flush final del archivo");
        }
    }
};

// Algoritmos de ordenamiento


//Intercambiar dos posiciones (funcion swap)
void intercambiar(PagedArray& arreglo, int a, int b) {
    int temporal = arreglo[a];
    arreglo[a] = arreglo[b];
    arreglo[b] = temporal;
}

// selectionsort
void selectionsort(PagedArray& arr, int n) {
    for (int i = 0; i < n - 1; i++) {
        int min_idx = i;

        for (int j = i + 1; j < n; j++) {
            if (arr[j] < arr[min_idx]) {
                min_idx = j;
            }
        }

        if (min_idx != i) {
            intercambiar(arr, i, min_idx);
        }
    }
}

// quicksort

//utiliza el ultimo elemento como pivote
int partition(PagedArray& arreglo, int low, int high) {
    int pivot = arreglo[high];
    int i = low - 1;

    for (int j = low; j <= high - 1; j++) {
        int actual = arreglo[j];
        if (actual <= pivot) {
            i++;
            intercambiar(arreglo, i, j);
        }
    }

    intercambiar(arreglo, i + 1, high);
    return i + 1;
}
//ordena de a la izq y der del pivote
void quicksort(PagedArray& arreglo, int low, int high) {
    if (low < high) {
        int pi = partition(arreglo, low, high);
        quicksort(arreglo, low, pi - 1);
        quicksort(arreglo, pi + 1, high);
    }
}


// mergesort

//une dos mitades ordenadas y las vuelve a escribir en el arreglo
void merge(PagedArray& arreglo, int izq, int mid, int der) {
    int n1 = mid - izq + 1;
    int n2 = der - mid;

    int* izquierda = new int[n1];
    int* derecha = new int[n2];

    for (int i = 0; i < n1; i++) {
        izquierda[i] = arreglo[izq + i];
    }

    for (int i = 0; i < n2; i++) {
        derecha[i] = arreglo[mid + 1 + i];
    }

    int i = 0;
    int j = 0;
    int k = izq;

    while (i < n1 && j < n2) {
        if (izquierda[i] <= derecha[j]) {
            arreglo[k] = izquierda[i];
            i++;
        } else {
            arreglo[k] = derecha[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        arreglo[k] = izquierda[i];
        i++;
        k++;
    }

    while (j < n2) {
        arreglo[k] = derecha[j];
        j++;
        k++;
    }

    delete[] izquierda;
    delete[] derecha;
}

//divide el arreglo hasta llegar a casos pequeños
void mergesort(PagedArray& arreglo, int izq, int der) {
    if (izq >= der) {
        return;
    }

    int mid = izq + (der - izq) / 2;
    mergesort(arreglo, izq, mid);
    mergesort(arreglo, mid + 1, der);
    merge(arreglo, izq, mid, der);
}

// countingsort

//countingsort se puede utilizar por son enteros en un rango determinado
void countingsort(PagedArray& arr, int n) {
    if (n <= 1) {
        return;
    }

    int minval = arr[0];
    int maxval = arr[0];

    for (int i = 1; i < n; i++) {
        int valor = arr[i];

        if (valor < minval) {
            minval = valor;
        }

        if (valor > maxval) {
            maxval = valor;
        }
    }

    int rango = maxval - minval + 1;
    int* conteo = new int[rango];

    for (int i = 0; i < rango; i++) {
        conteo[i] = 0;
    }

    for (int i = 0; i < n; i++) {
        conteo[arr[i] - minval]++;
    }

    int indice = 0;

    for (int i = 0; i < rango; i++) {
        while (conteo[i] > 0) {
            arr[indice] = i + minval;
            indice++;
            conteo[i]--;
        }
    }

    delete[] conteo;
}

// heapsort
void heapify(PagedArray& arr, int n, int i) {
    int largest = i;
    int l = 2 * i + 1;
    int r = 2 * i + 2;

    if (l < n && arr[l] > arr[largest]) {
        largest = l;
    }

    if (r < n && arr[r] > arr[largest]) {
        largest = r;
    }

    if (largest != i) {
        intercambiar(arr, i, largest);
        heapify(arr, n, largest);
    }
}

void heapsort(PagedArray& arr, int n) {
    for (int i = n / 2 - 1; i >= 0; i--) {
        heapify(arr, n, i);
    }

    for (int i = n - 1; i > 0; i--) {
        intercambiar(arr, 0, i);
        heapify(arr, i, 0);
    }
}

// introsort

//INsertionsort para subarreglos pequeños
void insertionSortRange(PagedArray& arreglo, int low, int high) {
    for (int i = low + 1; i <= high; i++) {
        int clave = arreglo[i];
        int j = i - 1;

        while (j >= low && arreglo[j] > clave) {
            arreglo[j + 1] = arreglo[j];
            j--;
        }

        arreglo[j + 1] = clave;
    }
}
//heapify como el de heapsort pero aplicado a partes especificas del arreglo
void heapifyRange(PagedArray& arr, int low, int heapSize, int root) {
    int largest = root;
    int left = 2 * root + 1;
    int right = 2 * root + 2;

    if (left < heapSize && arr[low + left] > arr[low + largest]) {
        largest = left;
    }

    if (right < heapSize && arr[low + right] > arr[low + largest]) {
        largest = right;
    }

    if (largest != root) {
        intercambiar(arr, low + root, low + largest);
        heapifyRange(arr, low, heapSize, largest);
    }
}

//heapsort aplicado a un solo subarreglo
void heapsortRange(PagedArray& arr, int low, int high) {
    int n = high - low + 1;

    for (int i = n / 2 - 1; i >= 0; i--) {
        heapifyRange(arr, low, n, i);
    }

    for (int i = n - 1; i > 0; i--) {
        intercambiar(arr, low, low + i);
        heapifyRange(arr, low, i, 0);
    }
}

//partition con pivote para el introsort
int partitionIntro(PagedArray& arreglo, int low, int high) {
    int pivot = arreglo[high];
    int i = low - 1;

    for (int j = low; j < high; j++) {
        if (arreglo[j] <= pivot) {
            i++;
            intercambiar(arreglo, i, j);
        }
    }

    intercambiar(arreglo, i + 1, high);
    return i + 1;
}

//limite de profundidad para evitar el peor caso de quicksort
int profundidadMaxima(int n) {
    return 2 * (int)log2(n);
}
//si el tramo es pequeño usa insertion, si la profundidad es alta pasa a heap y si no sigue econ quick
void introsortUtil(PagedArray& arreglo, int low, int high, int depthLimit) {
    int size = high - low + 1;

    if (size <= 16) {
        insertionSortRange(arreglo, low, high);
        return;
    }

    if (depthLimit == 0) {
        heapsortRange(arreglo, low, high);
        return;
    }

    int pivot = partitionIntro(arreglo, low, high);

    introsortUtil(arreglo, low, pivot - 1, depthLimit - 1);
    introsortUtil(arreglo, pivot + 1, high, depthLimit - 1);
}

//funcion principal de introsort
void introsort(PagedArray& arreglo, int n) {
    if (n <= 1) {
        return;
    }

    int depthLimit = profundidadMaxima(n);
    introsortUtil(arreglo, 0, n - 1, depthLimit);
}
//revisar orden
bool verificarOrden(const string& filename) {
    ifstream archivo(filename, ios::binary);

    if (!archivo.is_open()) {
        cerr << "Error al abrir archivo para verificacion" << endl;
        return false;
    }

    int anterior, actual;

    if (!archivo.read((char*)&anterior, sizeof(int))) {
        archivo.close();
        return true;
    }

    while (archivo.read((char*)&actual, sizeof(int))) {
        if (actual < anterior) {
            cerr << "Desorden detectado: " << anterior << " > " << actual << endl;
            archivo.close();
            return false;
        }
        anterior = actual;
    }

    archivo.close();
    return true;
}

string construirNombreArchivoLegible(const string& output) {
    size_t punto = output.find_last_of('.'); //eliminar la extension de archivo

    if (punto != string::npos) {
        return output.substr(0, punto) + ".txt";
    }

    return output + ".txt"; //agrerar el .txt
}

//Archivo legible
bool generarArchivoLegible(const string& archivoBinario, const string& archivoLegible) {
    ifstream archivoEntrada(archivoBinario, ios::binary);
    if (!archivoEntrada.is_open()) {
        cerr << "Error al abrir archivo binario" << endl;
        return false;
    }

    ofstream archivoSalida(archivoLegible);
    if (!archivoSalida.is_open()) {
        cerr << "Error al abrir archivo legible" << endl;
        return false;
    }

    int value;
    bool primero = true;

    while (archivoEntrada.read((char*)&value, sizeof(int))) { //leer el valor y pasarlo a entero
        if (!primero) {
            archivoSalida << ","; //para evitar una , al inicio
        }

        archivoSalida << value; //escribir el valor como entero
        primero = false;
    }

    archivoEntrada.close();
    archivoSalida.close();
    return true;
}
//csv de resultados
bool guardarResultadoCSV(const string& nombreCSV,
                         const string& algoritmo,
                         const string& input,
                         const string& output,
                         int pageSize,
                         int pageCount,
                         int totalElementos,
                         long long tiempoOrdenamientoMs,
                         long long tiempoTotalMs,
                         long long hits,
                         long long faults,
                         bool ordenado,
                         bool archivoLegibleGenerado) {
    bool escribirEncabezado = false;

    if (!filesystem::exists(nombreCSV) || filesystem::is_empty(nombreCSV)) {
        escribirEncabezado = true;
    }

    ofstream archivoCSV(nombreCSV, ios::app);
    if (!archivoCSV.is_open()) {
        cerr << "Error al abrir el archivo CSV" << endl;
        return false;
    }

    if (escribirEncabezado) {
        archivoCSV << "algoritmo,input,output,pageSize,pageCount,totalElementos,tiempo_ordenamiento_ms,tiempo_total_ms,hits,faults,ordenado,archivo_legible_generado\n";
    }

    archivoCSV << algoritmo << ","
               << input << ","
               << output << ","
               << pageSize << ","
               << pageCount << ","
               << totalElementos << ","
               << tiempoOrdenamientoMs << ","
               << tiempoTotalMs << ","
               << hits << ","
               << faults << ","
               << (ordenado ? "true" : "false") << ","
               << (archivoLegibleGenerado ? "true" : "false") << "\n";

    archivoCSV.close();
    return true;
}
//copiar archivo para trabajar en el output
bool copiarArchivoBinario(const string& origen, const string& destino) {
    ifstream archivoEntrada(origen, ios::binary);
    if (!archivoEntrada.is_open()) {
        cerr << "Error al abrir el archivo de entrada" << endl;
        return false;
    }

    ofstream archivoSalida(destino, ios::binary);
    if (!archivoSalida.is_open()) {
        cerr << "Error al abrir el archivo de salida" << endl;
        return false;
    }

    archivoSalida << archivoEntrada.rdbuf(); //copiar con un bufer

    archivoEntrada.close();
    archivoSalida.close();
    return true;
}


void imprimirResumen(const string& nombreAlgoritmo,
                     long long tiempoOrdenamientoMs,
                     long long tiempoTotalMs,
                     PagedArray& arreglo) {
    cout << "Tiempo del algoritmo: " << tiempoOrdenamientoMs << " ms" << endl;
    cout << "Tiempo del programa: " << tiempoTotalMs << " ms" << endl;
    cout << "Algoritmo: " << nombreAlgoritmo << endl;
    cout << "Total: " << arreglo.getTotalElementos() << endl;

    if (arreglo.getTotalElementos() > 0) {
        cout << "Primer elemento: " << (int)arreglo[0] << endl;
    }
    if (arreglo.getTotalElementos() > 1) {
        cout << "Segundo elemento: " << (int)arreglo[1] << endl;
    }
    if (arreglo.getTotalElementos() > 2) {
        cout << "Tercer elemento: " << (int)arreglo[2] << endl;
    }

    cout << "Hits: " << arreglo.getHits() << endl;
    cout << "Faults: " << arreglo.getFaults() << endl;
}

bool convertirEnteroPositivo(const char* texto, int& valor) {
    try {
        string s = texto;
        size_t pos = 0;
        int numero = stoi(s, &pos);

        if (pos != s.size()) {
            return false;
        }

        if (numero <= 0) {
            return false;
        }

        valor = numero;
        return true;
    } catch (...) {
        return false;
    }
}

bool validarArgumentos(int argc,
                       char* argv[],
                       string& input,
                       string& output,
                       string& algoritmo,
                       int& pageSize,
                       int& pageCount) {
    if (argc != 11) {
        cerr << "sorter -input <archivo_entrada> -output <archivo_salida> -alg <algoritmo> -pageSize <page_size> -pageCount <page_count>" << endl;
        return false;
    }

    if (string(argv[1]) != "-input") {
        cerr << "Falta -input" << endl;
        return false;
    }

    if (string(argv[3]) != "-output") {
        cerr << "Falta -output" << endl;
        return false;
    }

    if (string(argv[5]) != "-alg") {
        cerr << "Falta -alg" << endl;
        return false;
    }

    if (string(argv[7]) != "-pageSize") {
        cerr << "Falta -pageSize" << endl;
        return false;
    }

    if (string(argv[9]) != "-pageCount") {
        cerr << "Falta -pageCount" << endl;
        return false;
    }

    input = argv[2];
    output = argv[4];
    algoritmo = argv[6];

    if (!filesystem::exists(input)) {
        cerr << "el archivo de input no existe" << endl;
        return false;
    }

    if (!convertirEnteroPositivo(argv[8], pageSize)) {
        cerr << "pageSize debe ser un numero entero positivo " << endl;
        return false;
    }

    if (!convertirEnteroPositivo(argv[10], pageCount)) {
        cerr << "pageCount debe ser un numero entero positivo" << endl;
        return false;
    }

    if (algoritmo != "intro" &&
        algoritmo != "quick" &&
        algoritmo != "merge" &&
        algoritmo != "counting" &&
        algoritmo != "heap") {
        cerr << "algoritmo no reconocido" << endl;
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    long long tiempoOrdenamientoMs = 0;
    long long tiempoTotalMs = 0;
    int totalElementos = 0;
    long long hitsFinales = 0;
    long long faultsFinales = 0;

    string input;
    string output;
    string algoritmo;
    int pageSize = 0;
    int pageCount = 0;

    if (!validarArgumentos(argc, argv, input, output, algoritmo, pageSize, pageCount)) {
        return 1;
    }

    string algoritmoUsado = "";

    auto inicioTotal = high_resolution_clock::now();

    if (!copiarArchivoBinario(input, output)) {
        return 1;
    }

    try {
        PagedArray arreglo(output, pageSize, pageCount);
        int n = arreglo.size();

        auto inicioOrdenamiento = high_resolution_clock::now();

        if (algoritmo == "intro") {
            algoritmoUsado = "introsort";
            introsort(arreglo, n);
        } else if (algoritmo == "quick") {
            algoritmoUsado = "quicksort";
            quicksort(arreglo, 0, n - 1);
        } else if (algoritmo == "merge") {
            algoritmoUsado = "mergesort";
            mergesort(arreglo, 0, n - 1);
        } else if (algoritmo == "counting") {
            algoritmoUsado = "countingsort";
            countingsort(arreglo, n);
        } else if (algoritmo == "heap") {
            algoritmoUsado = "heapsort";
            heapsort(arreglo, n);
        }

        auto finOrdenamiento = high_resolution_clock::now();
        tiempoOrdenamientoMs = duration_cast<milliseconds>(finOrdenamiento - inicioOrdenamiento).count();

        totalElementos = arreglo.getTotalElementos();
        hitsFinales = arreglo.getHits();
        faultsFinales = arreglo.getFaults();

        auto finTotal = high_resolution_clock::now();
        tiempoTotalMs = duration_cast<milliseconds>(finTotal - inicioTotal).count();

        imprimirResumen(algoritmoUsado, tiempoOrdenamientoMs, tiempoTotalMs, arreglo);
    } catch (const exception& e) {
        cerr << "Error durante la ejecución: " << e.what() << endl;
        return 1;
    }

    bool ordenado = verificarOrden(output);
    if (ordenado) {
        cout << "Ordenado" << endl;
    } else {
        cout << "Desordenado" << endl;
    }

    string outputLegible = construirNombreArchivoLegible(output);
    bool archivoLegibleGenerado = generarArchivoLegible(output, outputLegible);
    if (!archivoLegibleGenerado) {
        cerr << "No se pudo generar el archivo legible." << endl;
        return 1;
    }

    bool csvGuardado = guardarResultadoCSV("resultadossorter.csv",
                                           algoritmoUsado,
                                           input,
                                           output,
                                           pageSize,
                                           pageCount,
                                           totalElementos,
                                           tiempoOrdenamientoMs,
                                           tiempoTotalMs,
                                           hitsFinales,
                                           faultsFinales,
                                           ordenado,
                                           archivoLegibleGenerado);

    if (!csvGuardado) {
        cerr << "No se pudo guardar el CSV." << endl;
        return 1;
    }

    return 0;
}