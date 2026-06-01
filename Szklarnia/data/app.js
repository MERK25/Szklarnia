const domyslneProfile = {
    'pomidory': { th: 40, time: 3, aggr: 1, name: 'Pomidory' }, 'ogorki': { th: 65, time: 1.5, aggr: 2, name: 'Ogórki' },
    'sadzonki': { th: 55, time: 0.5, aggr: 1, name: 'Sadzonki' }, 'arbuz': { th: 45, time: 4, aggr: 1, name: 'Arbuz' }, 'borowka': { th: 70, time: 1, aggr: 0, name: 'Borówka' }
};

let profileData = {};
try { const saved = localStorage.getItem('bazaProfili'); profileData = saved ? JSON.parse(saved) : domyslneProfile; } 
catch(e) { profileData = domyslneProfile; }

if (!profileData['arbuz']) profileData['arbuz'] = domyslneProfile['arbuz'];
if (!profileData['borowka']) profileData['borowka'] = domyslneProfile['borowka'];

let liveSoil = 0, liveUs = 0, liveLight = 0;

function aktualizujEtykieteAgresywnosci(val) {
    const etykiety = ["Oszczędny", "Zbalansowany", "Precyzyjny"];
    document.getElementById('edit-aggr-val').innerText = etykiety[val] || "Błąd";
}

function zaktualizujUIZESP(th, time, aggr) {
    document.getElementById('edit-th').value = th; document.getElementById('edit-th-val').innerText = th + '%';
    document.getElementById('edit-time').value = time; document.getElementById('edit-time-val').innerText = time + ' min';
    document.getElementById('edit-aggr').value = aggr; aktualizujEtykieteAgresywnosci(aggr);

    document.querySelectorAll('.profile-btn').forEach(btn => btn.classList.remove('selected'));
    let znalezionoProfil = false;
    for (const [id, param] of Object.entries(profileData)) {
        if (param.th == th && param.time == time && param.aggr == aggr) {
            document.getElementById('prof-' + id).classList.add('selected');
            localStorage.setItem("activeProfile", 'prof-' + id);
            znalezionoProfil = true; break;
        }
    }
    if (!znalezionoProfil) localStorage.removeItem("activeProfile");
}

function aktywujProfil(id) { ustawProfilZapisz(profileData[id].th, profileData[id].time, profileData[id].aggr, 'prof-' + id, profileData[id].name); }

function wczytajProfilDoEdycji() {
    const p = profileData[document.getElementById('edit-profile-select').value];
    document.getElementById('edit-th').value = p.th; document.getElementById('edit-th-val').innerText = p.th + '%';
    document.getElementById('edit-time').value = p.time; document.getElementById('edit-time-val').innerText = p.time + ' min';
    document.getElementById('edit-aggr').value = p.aggr !== undefined ? p.aggr : 1; aktualizujEtykieteAgresywnosci(p.aggr !== undefined ? p.aggr : 1);
}

function zapiszEdytowanyProfil() {
    haptyka(15);
    const id = document.getElementById('edit-profile-select').value;
    profileData[id].th = document.getElementById('edit-th').value;
    profileData[id].time = document.getElementById('edit-time').value;
    profileData[id].aggr = document.getElementById('edit-aggr').value;
    localStorage.setItem('bazaProfili', JSON.stringify(profileData));
    alert(`✅ Zapisano nowe parametry dla: ${profileData[id].name}`);
    if(document.getElementById('prof-' + id).classList.contains('selected')) aktywujProfil(id);
}

function zapiszGlebe(typ) {
    haptyka(15); let url = `/calibrate?`;
    if (typ === 'air') url += `air=${liveSoil}`; if (typ === 'water') url += `water=${liveSoil}`;
    fetch(url).then(() => { alert("✅ Zapisano wartość kalibracji gleby!"); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline"));
}

function zapiszWode(typ) {
    haptyka(15); let url = `/calibrateExtras?`;
    if (typ === 'empty') url += `usMax=${liveUs}`; if (typ === 'full') url += `usMin=${liveUs}`;
    fetch(url).then(() => { alert("✅ Zapisano nowy poziom dla beczki!"); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline"));
}

function zapiszSwiatlo() { haptyka(15); fetch(`/calibrateExtras?lMax=${liveLight}`).then(() => { alert("✅ Skalibrowano na 100% słońca!"); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline")); }
function zapiszKorekteTemp() { haptyka(15); const tOff = document.getElementById('calib-tOff').value; fetch(`/calibrateExtras?tOff=${tOff}`).then(() => { alert("✅ Zapisano przesunięcie temperatury"); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline")); }

let stoperInterval, stoperStartTime, stoperRunning = false;
function toggleStoper() {
    const btn = document.getElementById('btn-stoper'), display = document.getElementById('stopwatch-display'), result = document.getElementById('stoper-result');
    if(!stoperRunning) {
        stoperStartTime = Date.now(); stoperRunning = true; btn.innerText = "🛑 Zatrzymaj (Mam pełny 1 Litr)"; btn.style.background = "var(--accent-red)"; btn.style.color = "white"; result.style.display = "none";
        stoperInterval = setInterval(() => { let elapsed = (Date.now() - stoperStartTime) / 1000; display.innerText = elapsed.toFixed(1) + "s"; }, 100); haptyka(15);
    } else {
        clearInterval(stoperInterval); stoperRunning = false; btn.innerText = "⏱ Start Stopera"; btn.style.background = "var(--accent-blue)";
        let finalTime = (Date.now() - stoperStartTime) / 1000; let lPerMin = 60 / finalTime;
        result.innerText = `Wydajność pompy: ${lPerMin.toFixed(2)} L/min`; result.style.display = "block";
        document.getElementById('pump-flow').value = lPerMin.toFixed(2); zapiszParametryZbiornika(); haptyka([50, 100, 50]);
    }
}

function zapiszParametryZbiornika() {
    haptyka(10); const vol = document.getElementById('tank-vol').value, flow = document.getElementById('pump-flow').value;
    fetch(`/tank?vol=${vol}&flow=${flow}`).then(() => { alert("Zapisano parametry zbiornika na ESP32."); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline"));
}

function zresetujLicznikWody() { haptyka([30, 30, 30]); if(confirm("Czy na pewno wyzerować licznik?")) fetch('/resetWater').then(() => { alert("✅ Licznik wyzerowany."); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline")); }
function wypompujWode() { haptyka([50, 50, 50]); if(confirm("Pompa będzie pracować aż do braku wody. Kontynuować?")) fetch('/drain').then(() => { alert("❄️ Rozpoczęto opróżnianie."); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline")); }
function haptyka(wzor) { if ("vibrate" in navigator) navigator.vibrate(wzor); }

function zmienMotyw(motyw) {
    haptyka(10);
    ['light', 'auto', 'dark'].forEach(m => document.getElementById('theme-' + m).classList.remove('active'));
    document.getElementById('theme-' + motyw).classList.add('active');
    if (motyw === 'auto') { document.documentElement.removeAttribute('data-theme'); localStorage.removeItem('theme'); }
    else { document.documentElement.setAttribute('data-theme', motyw); localStorage.setItem('theme', motyw); }
    const cachedData = localStorage.getItem('lastESPData'); if(cachedData) { try { const p = JSON.parse(cachedData); if(p.data && p.data.logs) rysujWykresy(p.data.logs); } catch(e) {} }
}

function zmienZakladke(tab) {
    haptyka(15);
    ['pulpit', 'ustawienia'].forEach(t => { document.getElementById('btn-' + t).classList.remove('active'); document.getElementById('tab-' + t).classList.remove('active'); });
    document.getElementById('btn-' + tab).classList.add('active'); document.getElementById('tab-' + tab).classList.add('active');
}

function zmienPodzakladke(tab) {
    haptyka(10);
    ['ogolne', 'zaawansowane'].forEach(t => { document.getElementById('btn-sub-' + t).classList.remove('active'); document.getElementById('sub-' + t).classList.remove('active'); });
    document.getElementById('btn-sub-' + tab).classList.add('active'); document.getElementById('sub-' + tab).classList.add('active');
}

function wyliczZegarAstronomiczny() {
    const lat = 52.0, lon = 19.0;
    const now = new Date();
    const dayOfYear = Math.floor((now - new Date(now.getFullYear(), 0, 0)) / 86400000);
    const gamma = (2 * Math.PI / 365) * (dayOfYear - 1 + 0.5);
    const eqTime = 229.18 * (0.000075 + 0.001868 * Math.cos(gamma) - 0.032077 * Math.sin(gamma) - 0.014615 * Math.cos(2 * gamma) - 0.040849 * Math.sin(2 * gamma));
    const decl = 0.006918 - 0.399912 * Math.cos(gamma) + 0.070257 * Math.sin(gamma) - 0.006758 * Math.cos(2 * gamma) + 0.000907 * Math.sin(2 * gamma) - 0.002697 * Math.cos(3 * gamma) + 0.00148 * Math.sin(3 * gamma);
    const ha = Math.acos((Math.cos(90.833 * Math.PI / 180) / (Math.cos(lat * Math.PI / 180) * Math.cos(decl))) - Math.tan(lat * Math.PI / 180) * Math.tan(decl));
    const sunriseUTC = 720 - 4 * (lon + ha * 180 / Math.PI) - eqTime;
    const sunsetUTC  = 720 - 4 * (lon - ha * 180 / Math.PI) - eqTime;
    const tzOffset = -now.getTimezoneOffset(); 
    let localSunrise = Math.round(sunriseUTC + tzOffset), localSunset = Math.round(sunsetUTC + tzOffset);
    if (localSunrise < 0) localSunrise += 1440; if (localSunset >= 1440) localSunset -= 1440;
    return { sr: localSunrise, ss: localSunset };
}

window.onload = function() {
    wczytajProfilDoEdycji(); zmienMotyw(localStorage.getItem('theme') || 'auto');
    const act = localStorage.getItem("activeProfile"); if(act && document.getElementById(act)) document.getElementById(act).classList.add('selected');
    const cachedData = localStorage.getItem('lastESPData');
    if(cachedData) { try { const parsed = JSON.parse(cachedData); renderujDane(parsed.data, parsed.timestamp, false); } catch(e) {} }
    else document.querySelectorAll('.weather-val').forEach(el => el.classList.add('skeleton'));
    const sunData = wyliczZegarAstronomiczny();
    fetch(`/sync?t=${Math.floor(Date.now() / 1000)}&sr=${sunData.sr}&ss=${sunData.ss}`).catch(()=>{});
    pobierzDaneZESP32();
}

function sformatujCzas(timestamp) {
    const minuty = Math.floor((new Date().getTime() - timestamp) / 60000);
    if (minuty < 1) return "przed chwilą"; if (minuty < 60) return `${minuty} min temu`;
    return `${Math.floor(minuty / 60)} godz. temu`;
}

function formatujMinuty(minutyOdPolnocy) {
    if(!minutyOdPolnocy || isNaN(minutyOdPolnocy)) return "-";
    const h = Math.floor(minutyOdPolnocy / 60).toString().padStart(2, '0');
    const m = (minutyOdPolnocy % 60).toString().padStart(2, '0');
    return `${h}:${m}`;
}

function rysujProstyWykres(canvasId, daneArray, kolorLinii, minVal, maxVal) {
    const canvas = document.getElementById(canvasId); if(!canvas) return; const ctx = canvas.getContext('2d');
    const rect = canvas.parentElement.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr; canvas.height = rect.height * dpr; ctx.scale(dpr, dpr);
    const drawW = rect.width - 20, drawH = rect.height - 20; ctx.clearRect(0, 0, rect.width, rect.height);
    if(!daneArray || daneArray.length === 0) return;
    const stepX = drawW / Math.max(1, daneArray.length - 1);
    ctx.beginPath(); ctx.strokeStyle = kolorLinii; ctx.lineWidth = 3; ctx.lineJoin = 'round';
    daneArray.forEach((val, i) => {
        let bVal = Math.max(minVal, Math.min(val, maxVal)); const percent = (bVal - minVal) / (maxVal - minVal);
        const x = 10 + (i * stepX), y = 10 + drawH - (percent * drawH);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }); ctx.stroke();
    ctx.fillStyle = kolorLinii;
    daneArray.forEach((val, i) => {
        let bVal = Math.max(minVal, Math.min(val, maxVal)); const percent = (bVal - minVal) / (maxVal - minVal);
        const x = 10 + (i * stepX), y = 10 + drawH - (percent * drawH);
        ctx.beginPath(); ctx.arc(x, y, 4, 0, Math.PI * 2); ctx.fill();
    });
}

function rysujWykresy(logi) {
    const dane = [...logi].reverse(), rs = getComputedStyle(document.documentElement);
    rysujProstyWykres('moistureChart', dane.map(l => l.moisture), rs.getPropertyValue('--accent-blue').trim()||'#007aff', 0, 100);
    rysujProstyWykres('tempChart', dane.map(l => l.t || 20), rs.getPropertyValue('--accent-red').trim()||'#ff3b30', 0, 50);
    rysujProstyWykres('humChart', dane.map(l => l.h || 50), rs.getPropertyValue('--accent-cyan').trim()||'#32ade6', 0, 100);
    rysujProstyWykres('batteryChart', dane.map(l => l.v || 3.5), rs.getPropertyValue('--accent-orange').trim()||'#ff9500', 3.2, 4.3);
}

function renderujDane(data, timestamp, trybDemo = false) {
    document.querySelectorAll('.weather-val').forEach(el => el.classList.remove('skeleton'));
    liveSoil = data.raw_adc; liveUs = data.us_dist; liveLight = data.raw_light;
    document.getElementById('live-soil-val').innerText = liveSoil || "---";
    document.getElementById('live-us-val').innerText = (liveUs !== undefined) ? liveUs + " cm" : "--- cm";
    document.getElementById('live-light-val').innerText = liveLight || "---";
    if(data.t_off !== undefined) document.getElementById("calib-tOff").value = data.t_off;

    if(data.cfg_th !== undefined && data.cfg_time !== undefined && data.cfg_aggr !== undefined) zaktualizujUIZESP(data.cfg_th, data.cfg_time, data.cfg_aggr);
    
    document.getElementById("dash-temp").innerText = (data.temp !== undefined) ? data.temp + "°" : "-";
    document.getElementById("dash-hum").innerText = (data.air_hum !== undefined) ? data.air_hum + "%" : "-";
    document.getElementById("dash-pres").innerText = (data.pressure !== undefined && data.pressure > 0) ? data.pressure.toFixed(0) + " hPa" : "-";
    document.getElementById("dash-water-temp").innerText = (data.water_temp && data.water_temp > 0) ? data.water_temp + "°" : "-";
    document.getElementById("dash-sun").innerText = (data.sun_hours !== undefined) ? data.sun_hours + " h" : "-";
    document.getElementById("dash-sunrise").innerText = formatujMinuty(data.sr);
    document.getElementById("dash-sunset").innerText = formatujMinuty(data.ss);

    if(data.pump_sec !== undefined) {
        let tVol = data.tank_vol || 220, fRate = data.pump_flow || 2.0, remLiters = 0, pct = 0;
        document.getElementById('tank-vol').value = tVol; document.getElementById('pump-flow').value = fRate;

        if (data.us_dist !== undefined && data.us_dist > 0 && data.us_max !== undefined && data.us_min !== undefined) {
            pct = Math.max(0, Math.min(100, Math.round(100 - (((data.us_dist - data.us_min) / (data.us_max - data.us_min)) * 100))));
            remLiters = (pct / 100.0) * tVol;
        } else {
            remLiters = Math.max(0, tVol - ((data.pump_sec / 60) * fRate));
            pct = Math.max(0, Math.min(100, Math.round((remLiters / tVol) * 100)));
        }
        
        document.getElementById("water-height-controller").style.bottom = (-800 + (100 * (pct / 100))) + "px";
        document.getElementById("dash-water-pct").innerText = pct + "%";
        document.getElementById("dash-water-liters").innerText = Math.round(remLiters) + " / " + tVol + " L";

        let estStr = "Brak danych";
        if (document.querySelector('.profile-btn.selected') && data.cfg_time) {
            let litersPerDay = ((data.cfg_aggr == 2) ? 4 : (data.cfg_aggr == 1 ? 3 : 2)) * (data.cfg_time * fRate);
            if (litersPerDay > 0) {
                let days = Math.floor(remLiters / litersPerDay);
                estStr = days > 14 ? "Zapas na ponad 2 tyg." : (days > 1 ? `Wystarczy na ok. ${days} dni` : (days === 1 ? `Wystarczy na 1 dzień` : `Woda się kończy!`));
            }
        }
        document.getElementById("dash-water-est").innerText = estStr;
        document.getElementById("dash-water-pct").style.color = pct <= 15 ? "var(--accent-red)" : "var(--text-main)";
        pct <= 15 ? document.getElementById("tank-mask").classList.add("alert") : document.getElementById("tank-mask").classList.remove("alert");
    }

    if(data.outdoors !== undefined) {
        document.getElementById('loc-in').classList.remove('active'); document.getElementById('loc-out').classList.remove('active');
        data.outdoors === 1 ? document.getElementById('loc-out').classList.add('active') : document.getElementById('loc-in').classList.add('active');
    }

    if (data.voltage) {
        document.getElementById("eng-voltage").innerText = data.voltage.toFixed(3) + " V";
        let pct = Math.max(0, Math.min(100, Math.round(((data.voltage - 3.2) / 1.0) * 100)));
        let bEl = document.getElementById("dash-batt"); bEl.innerText = (data.voltage >= 4.4 ? "⚡ " : "") + pct + "%"; bEl.style.color = pct <= 20 ? "var(--accent-red)" : "var(--text-main)";
        let hl = pct * 2, tEl = document.getElementById("dash-time"); tEl.innerText = Math.floor(hl/24) > 0 ? `${Math.floor(hl/24)}d ${hl%24}h` : `${hl}h`; tEl.style.color = pct <= 20 ? "var(--accent-red)" : "var(--text-main)";
    }

    document.getElementById("last-update-text").innerText = (timestamp ? `Aktualizacja: ${sformatujCzas(timestamp)}` : "Zaktualizowano przed chwilą") + (trybDemo ? " (Demo)" : "");

    const hI = document.getElementById("hero-icon"), hT = document.getElementById("hero-text"), uB = document.getElementById("fault-unlock-btn"), pC = document.getElementById("plants-container"), pT = document.getElementById("plants-title");
    uB.style.display = "none"; pC.style.display = "flex"; pT.style.display = "block";

    if(data.batt_warn === 1) { hI.innerText = "🔋"; hI.classList.add("pulse-alert"); hT.innerText = "Spadek pojemności Akumulatora!"; hT.style.color = "var(--accent-orange)"; }
    else if(data.survival === 1) { hI.innerText = "🔋"; hI.classList.add("pulse-alert"); hT.innerText = "Tryb przetrwania (Słaba Bateria)"; hT.style.color = "var(--accent-orange)"; }
    else if(data.fault === 1) { hI.innerText = "🔥"; hI.classList.add("pulse-alert"); hT.innerText = "AWARIA POMPY!"; hT.style.color = "var(--accent-red)"; uB.style.display = "block"; pC.style.display = "none"; pT.style.display = "none"; }
    else if(data.water === "EMPTY") { hI.innerText = "⚠️"; hI.classList.add("pulse-alert"); hT.innerText = "Dolej wody do beczki!"; hT.style.color = "var(--accent-red)"; }
    else if(data.hibernated === 1) { hI.innerText = "❄️"; hI.classList.remove("pulse-alert"); hT.innerText = "System zhibernowany"; hT.style.color = "var(--accent-blue)"; pC.style.display = "none"; pT.style.display = "none"; }
    else { hI.innerText = "🌿"; hI.classList.remove("pulse-alert"); hT.innerText = "Wszystko w porządku"; hT.style.color = "var(--accent-green)"; }

    const logBox = document.getElementById("logBox"); logBox.innerHTML = "";
    if(!data.logs || data.logs.length === 0) logBox.innerHTML = "<div class='log-item' style='justify-content:center;'>Brak wpisów.</div>";
    else {
        [...data.logs].reverse().forEach(log => {
            let cls = "bg-ok", a = log.action;
            if(a==="PODLANO") cls="bg-water"; if(a==="BRAK WODY!"||a==="AWARIA POMPY!") cls="bg-error";
            if(a==="ZA GORĄCO"||a==="BLOKADA (SŁOŃCE)"||a==="BLOKADA (BURZA)"||a==="TRYB SURVIVAL") cls="bg-warn"; 
            if(a==="RĘCZNE") cls="bg-manual"; if(a==="NOCNY SEN") cls="bg-sleep"; if(a==="ZIMNA WODA") cls="bg-cold"; if(a==="BLOKADA (SAUNA)") cls="bg-sauna"; 
            logBox.innerHTML += `<div class='log-item'><span>Wilg: <b>${log.moisture}%</b></span><span class='log-badge ${cls}'>${a}</span></div>`;
        }); rysujWykresy(data.logs);
    }
}

function pobierzDaneZESP32() {
    haptyka(10); const badge = document.getElementById('update-badge-btn'); if(badge) badge.classList.add('syncing');
    fetch('/data').then(r => r.json()).then(data => {
        localStorage.setItem('lastESPData', JSON.stringify({ data: data, timestamp: Date.now() })); renderujDane(data, Date.now(), false);
    }).catch(() => {
        const cached = localStorage.getItem('lastESPData'), ds = wyliczZegarAstronomiczny();
        if(!cached) renderujDane({temp: 32.5, air_hum: 88, pressure: 1012.5, voltage: 3.95, pump_sec: 450, fault: 0, survival: 0, water: "OK", outdoors: 0, water_temp: 18.5, sun_hours: 6.5, tank_vol: 220, pump_flow: 2.0, cfg_time: 2, cfg_aggr: 1, sr: ds.sr, ss: ds.ss, batt_warn: 0, us_max: 90, us_dist: 45, raw_adc: 2100, raw_light: 3500, logs: [{moisture: 42, v: 3.95, t: 32.1, h: 88, action: "BLOKADA (SAUNA)"}]}, Date.now(), true);
        else renderujDane(JSON.parse(cached).data, JSON.parse(cached).timestamp, true);
    }).finally(() => { if(badge) setTimeout(() => badge.classList.remove('syncing'), 500); });
}

function ustawProfilZapisz(th, time, aggr, btnId, name) {
    haptyka([15, 50, 15]); document.querySelectorAll('.profile-btn').forEach(btn => btn.classList.remove('selected'));
    document.getElementById(btnId).classList.add('selected'); localStorage.setItem("activeProfile", btnId);
    const toast = document.getElementById("pulpit-toast"); toast.style.opacity = "1"; toast.innerText = "⏳ Wysyłanie...";
    fetch(`/save?thPct=${th}&timeSec=${time * 60}&aggr=${aggr}`).then(() => { toast.innerText = "✓ Zapisano ustawienia: " + name; pobierzDaneZESP32(); }).catch(() => { toast.innerText = "✓ (Demo) Ustawiono lokalnie"; setTimeout(() => toast.style.opacity = "0", 3000); });
}

function ustawLokalizacje(isOutdoors) {
    haptyka(15); document.getElementById('loc-in').classList.remove('active'); document.getElementById('loc-out').classList.remove('active');
    isOutdoors === 1 ? document.getElementById('loc-out').classList.add('active') : document.getElementById('loc-in').classList.add('active');
    fetch(`/location?outdoors=${isOutdoors}`).then(() => alert("Zaktualizowano profil.")).catch(() => alert("⚠️ Tryb Offline"));
}

function uruchomTest() { haptyka(15); const sec = document.getElementById("testSlider").value; fetch(`/test?sec=${sec}`).then(() => alert(`🌊 Test pompy (${sec}s) aktywny.`)).catch(() => alert("❌ Tryb offline.")); }

function eksportujCSV() {
    haptyka(15); const cached = localStorage.getItem('lastESPData'); if(!cached) return alert("Brak danych.");
    const logs = JSON.parse(cached).data.logs; if(!logs) return;
    let csv = "data:text/csv;charset=utf-8,ID,Wilgotnosc_Pct,Temperatura_C,Wilgotnosc_Powietrza_Pct,Napiecie_V,Zarejestrowana_Akcja\n"; 
    logs.forEach((l, i) => csv += `${i + 1},${l.moisture},${l.t || "-"},${l.h || "-"},${l.v || "-"},${l.action}\n`);
    const link = document.createElement("a"); link.setAttribute("href", encodeURI(csv)); link.setAttribute("download", "szklarnia_raport.csv"); document.body.appendChild(link); link.click(); document.body.removeChild(link);
}

function odblokujPompe() { haptyka([50, 50, 50]); if(confirm("Usunąłeś awarię fizyczną. Odblokować?")) fetch('/resetFault').then(() => { alert("✅ Odblokowano"); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline")); }
function aktywujHibernacje() { haptyka([100, 100, 100]); if(confirm("Uśpić szklarnię na stałe?")) fetch('/hibernate').then(() => { alert("❄️ Urządzenie uśpione."); pobierzDaneZESP32(); }).catch(() => alert("⚠️ Tryb Offline")); }

function wgrywajOTA() {
    const file = document.getElementById('ota-file').files[0]; if(!file || !file.name.endsWith('.bin')) return alert("Wybierz plik .bin!");
    haptyka([20, 50, 20]); const status = document.getElementById('ota-status'), bar = document.getElementById('ota-progress-bar');
    status.innerText = "Nawiązywanie połączenia..."; document.getElementById('ota-progress-container').style.display = "block"; bar.style.width = "0%";
    const fd = new FormData(); fd.append("update", file, file.name); const xhr = new XMLHttpRequest(); xhr.open("POST", "/update", true);
    xhr.upload.addEventListener("progress", e => { if (e.lengthComputable) { bar.style.width = (e.loaded / e.total) * 100 + "%"; status.innerText = `Przesyłanie: ${Math.round((e.loaded / e.total) * 100)}%`; }});
    xhr.onload = () => { if (xhr.status === 200) { status.innerText = "✅ Sukces! Restart..."; bar.style.background = "var(--accent-green)"; haptyka([50, 100, 50, 100, 50]); } else status.innerText = "❌ Błąd."; };
    xhr.onerror = () => { status.innerText = "❌ Błąd sieci."; bar.style.background = "var(--accent-red)"; }; xhr.send(fd);
}

window.addEventListener('resize', () => { const cached = localStorage.getItem('lastESPData'); if(cached) { try { const p = JSON.parse(cached); if(p.data && p.data.logs) rysujWykresy(p.data.logs); } catch(e) {} } });
</script>