#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
using namespace std;
using namespace std::chrono;


int main(int n, char* argv[]) {

    auto inicio= high_resolution_clock::now();
    string opcion = argv[2];
    string archivo = argv[4];

    ofstream archivo2(archivo, std::ios::out | std::ios::binary | std::ios::trunc); //abrir el archivo para escribir

    if (!archivo2) {
        cout << "Error al abrir el archivo." << endl;
        return 1;
    }


    if (opcion == "small") {
        cout<<"pequeno"<<endl;
        for (int i = 0; i < 134217728; i++) {
            int x = rand() % 9000+1000;
            archivo2.write((char*)&x, sizeof(x));
        }
    }
    else  if (opcion == "medium") {
        cout<<"mediano"<<endl;
        for (int i = 0; i < 268435456; i++) {
            int x = rand() % 9000+1000;
            archivo2.write((char*)&x, sizeof(x));
        }
    }
    else {
        for (int i = 0; i < 536870912; i++) {

            int x = rand() % 9000+1000;
            archivo2.write((char*)&x, sizeof(x));
        }
        cout<<"grande"<<endl;
    }
    auto fin = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(fin - inicio);
    cout<<duration.count()<<endl;
    return 0;

}