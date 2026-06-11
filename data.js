const icons = { "ko": "🥕", "kw": "🌻", "li": "🥬", "ow": "🍎" };

const typeMap = {
    "ko": { action: "Rośliny korzeniowe", details: "Dobry dzień m.in. na marchew, buraki, rzodkiewkę, seler i cebulę." },
    "kw": { action: "Rośliny kwiatowe", details: "Dobry dzień m.in. na słoneczniki, róże oraz wszelkie kwiaty cięte i ozdobne." },
    "li": { action: "Rośliny liściowe", details: "Dobry dzień m.in. na sałatę, kapustę, szpinak, jarmuż i zioła liściaste." },
    "ow": { action: "Rośliny owocowe", details: "Dobry dzień m.in. na pomidory, ogórki, fasolę, dynię oraz drzewa owocowe." }
};

// Pełen rok 2026, każda linijka to jeden miesiąc (od stycznia do grudnia).
const yearData = [
    "ko,kw,kw,li,ow,ow,ow,ko,ko,ko,ko,kw,li,li,li,li,ow,ow,ko,ko,kw,kw,li,li,ow,ow,ko,ko,ko,kw,li", 
    "li,ow,ow,ko,ko,ko,ko,kw,kw,li,li,ow,ow,ow,ko,ko,kw,kw,li,li,li,ow,ko,ko,kw,kw,kw,li", 
    "ow,ow,ow,ko,ko,ko,ko,kw,li,li,li,ow,ow,ko,ko,kw,kw,li,li,li,ow,ko,ko,ko,kw,kw,li,li,ow,ow,ko", 
    "ko,ko,ko,ko,li,li,li,ow,ow,ow,ko,ko,kw,kw,li,li,ow,ow,ko,ko,kw,li,ow,ow,ow,ko,ko,ko,ko,ko", 
    "kw,li,ow,ow,ko,ko,ko,li,ow,ow,ko,kw,li,li,ow,ko,ko,ko,kw,kw,li,li,ow,ow,ko,ko,kw,kw,li,li,li", 
    "ow,ow,ow,ko,ko,kw,kw,li,li,li,ow,ow,ko,ko,kw,li,li,ow,ow,ow,ko,ko,ko,ko,kw,li,li,li,ow,ow", 
    "ko,ko,ko,kw,kw,li,li,ow,ow,ko,ko,ko,kw,li,li,ow,ow,ko,ko,ko,kw,kw,kw,li,li,ow,ow,ow,ko,ko,kw", 
    "kw,li,li,li,ow,li,ko,ko,ko,li,ow,ow,ow,ko,ko,ko,kw,kw,li,li,li,ow,ow,ko,ko,kw,kw,li,li,li,li", 
    "ow,ow,ko,ko,kw,kw,li,li,ow,ow,ko,ko,ko,ko,kw,kw,li,li,ow,ow,ko,ko,ko,kw,li,li,ow,ow,ko,ko", 
    "ko,li,kw,li,li,ow,ow,ko,ko,ko,kw,li,li,ow,ow,ow,ko,ko,ko,kw,kw,li,li,li,ow,ow,ko,ko,ko,kw,kw", 
    "li,ow,ow,ow,ko,ko,ko,kw,kw,li,li,ow,ow,ow,ko,ko,kw,kw,li,li,ow,ow,li,ko,ko,kw,kw,li,li,ow", 
    "ow,ko,ko,ko,kw,kw,li,li,li,ow,ow,ko,ko,ko,kw,kw,li,li,ow,ow,ko,ko,ko,kw,li,li,ow,ow,ko,ko,ow"
];