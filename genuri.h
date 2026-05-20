#ifndef GENURI_H
#define GENURI_H

#include <vector>
#include <string>

using namespace std;

// Lista completă a tuturor genurilor disponibile în sistem
vector<string> getToateGenurile() {
    return {
        // Genuri filme
        "Acțiune", "Aventură", "Animatie", "Comedie", "Crimă", "Documentar", 
        "Dramă", "Familie", "Fantastic", "Istoric", "Horror", "Muzical",
        "Mister", "Romantic", "SF", "Thriller", "Război", "Western",
        
        // Genuri cărți
        "Ficțiune", "Non-ficțiune", "Biografie", "Autobiografie", "Poezie",
        "Teatru", "Eseu", "Roman", "Nuvele", "Povestire", "Literatură clasică",
        "Literatură contemporană", "Literatură de dragoste", "Literatură istorică",
        "Literatură fantastică", "Literatură SF", "Literatură horror",
        "Literatură polițistă", "Literatură de aventură", "Literatură pentru copii",
        
        // Genuri muzică
        "Pop", "Rock", "Hip-Hop", "Rap", "Electronică", "Dance", "Club",
        "House", "Techno", "Trance", "Dubstep", "Drum&Bass", "Jazz", "Blues",
        "Soul", "Funk", "R&B", "Reggae", "Latino", "Country", "Folk",
        "Clasică", "Operă", "Metal", "Punk", "Alternative", "Indie",
        "Gospel", "Religioasă", "Soundtrack", "Instrumentală"
    };
}

#endif