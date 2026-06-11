let currentDate = new Date(2026, 0, 1);

function renderCalendar(date) {
    const year = date.getFullYear();
    const month = date.getMonth();
    
    const monthNames = ['Styczeń', 'Luty', 'Marzec', 'Kwiecień', 'Maj', 'Czerwiec', 'Lipiec', 'Sierpień', 'Wrzesień', 'Październik', 'Listopad', 'Grudzień'];
    
    if(year !== 2026) {
        document.getElementById('month-year').textContent = `${monthNames[month]} ${year}`;
        document.getElementById('calendar-grid').innerHTML = '<div style="grid-column: 1/-1; text-align: center; padding: 20px;">Brak danych kalendarza dla tego roku.</div>';
        return;
    }

    document.getElementById('month-year').textContent = `${monthNames[month]} ${year}`;
    
    const firstDay = new Date(year, month, 1).getDay();
    const daysInMonth = new Date(year, month + 1, 0).getDate();
    
    const grid = document.getElementById('calendar-grid');
    grid.innerHTML = '';
    
    let startDay = firstDay === 0 ? 6 : firstDay - 1;
    
    for (let i = 0; i < startDay; i++) {
        const emptyDiv = document.createElement('div');
        emptyDiv.className = 'day empty';
        grid.appendChild(emptyDiv);
    }
    
    const monthDataStr = yearData[month].split(',');

    for (let day = 1; day <= daysInMonth; day++) {
        const dayDiv = document.createElement('div');
        dayDiv.className = 'day';
        dayDiv.textContent = day;
        
        const dayCode = monthDataStr[day - 1];
        const dayData = typeMap[dayCode];
        
        if (dayData) {
            const iconDiv = document.createElement('div');
            iconDiv.className = 'day-icon';
            iconDiv.textContent = icons[dayCode];
            dayDiv.appendChild(iconDiv);
            
            dayDiv.addEventListener('click', () => openModal(day, monthNames[month], year, dayData, icons[dayCode]));
        }
        
        grid.appendChild(dayDiv);
    }
}

function openModal(day, monthName, year, data, icon) {
    document.getElementById('modal-date').textContent = `${day} ${monthName} ${year}`;
    document.getElementById('modal-big-icon').textContent = icon;
    document.getElementById('modal-action').textContent = data.action;
    document.getElementById('modal-details').textContent = data.details;
    document.getElementById('day-modal').classList.remove('hidden');
}

document.getElementById('close-modal').addEventListener('click', () => {
    document.getElementById('day-modal').classList.add('hidden');
});

document.getElementById('prev-month').addEventListener('click', () => {
    currentDate.setMonth(currentDate.getMonth() - 1);
    renderCalendar(currentDate);
});

document.getElementById('next-month').addEventListener('click', () => {
    currentDate.setMonth(currentDate.getMonth() + 1);
    renderCalendar(currentDate);
});

renderCalendar(currentDate);

// Rejestracja Service Workera (PWA - tryb offline)
if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
        navigator.serviceWorker.register('./sw.js').catch(console.error);
    });
}