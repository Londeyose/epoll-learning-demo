const canvas = document.getElementById('game-canvas');
const hpEl = document.getElementById('hp');
const scoreEl = document.getElementById('score');
const overlay = document.getElementById('overlay');
const startBtn = document.getElementById('start-btn');
const finalScoreEl = document.getElementById('final-score');
const titleEl = document.getElementById('title');
const leaderboardArea = document.getElementById('leaderboard');
const scoresList = document.getElementById('scores-list');
const userNameEl = document.getElementById('user-name');

let hp = 100;
let score = 0;
let gameActive = false;
let spawnRate = 1200;

const bugTypes = ["NullPointer", "StackOverflow", "Timeout", "SegFault", "SyntaxError", "MemoryLeak"];

// 静态排行榜数据
const leaderboardData = [
    { name: "Niko", score: 1000 },
    { name: "Jack", score: 890 },
    { name: "Apex", score: 500 }
];

async function loadCurrentUser() {
    try {
        const resp = await fetch('/whoami');
        if (!resp.ok) {
            return;
        }
        const data = await resp.json();
        if (data && data.ok && data.username && userNameEl) {
            userNameEl.innerText = data.username;
        }
    } catch (err) {
        // 读取用户信息失败时保持默认 Guest。
    }
}

loadCurrentUser();

startBtn.addEventListener('click', () => {
    hp = 100;
    score = 0;
    spawnRate = 1200;
    gameActive = true;

    canvas.innerHTML = '';
    hpEl.innerText = hp;
    scoreEl.innerText = score;
    hpEl.style.color = '#58a6ff';

    // 重置 UI
    overlay.style.display = 'none';
    finalScoreEl.style.display = 'none';
    leaderboardArea.style.display = 'none';
    titleEl.innerText = 'SYSTEM ERROR';

    spawnLoop();
});

function spawnLoop() {
    if (!gameActive) return;
    createBug();
    const nextSpawn = Math.max(400, spawnRate - score / 5);
    setTimeout(spawnLoop, nextSpawn);
}

function createBug() {
    if (!gameActive) return;

    const bug = document.createElement('div');
    bug.className = 'bug';
    bug.innerText = bugTypes[Math.floor(Math.random() * bugTypes.length)];
    canvas.appendChild(bug);

    const x = Math.random() * (canvas.clientWidth - bug.offsetWidth - 20) + 10;
    const y = Math.random() * (canvas.clientHeight - bug.offsetHeight - 20) + 10;
    bug.style.left = `${x}px`;
    bug.style.top = `${y}px`;

    bug.onclick = (e) => {
        e.stopPropagation();
        score += 10;
        scoreEl.innerText = score;
        bug.remove();
    };

    // 2 秒不处理自动爆炸并掉血
    setTimeout(() => {
        if (bug.parentElement && gameActive) {
            bug.remove();
            takeDamage();
        }
    }, 2000);
}

function takeDamage() {
    hp -= 20;
    if (hp <= 0) {
        hp = 0;
    }

    hpEl.innerText = hp;
    hpEl.style.color = hp <= 40 ? '#f85149' : '#58a6ff';

    if (hp <= 0) {
        gameOver();
    }
}

function gameOver() {
    gameActive = false;

    overlay.style.display = 'flex';
    titleEl.innerText = 'SYSTEM CRASHED';

    finalScoreEl.style.display = 'block';
    finalScoreEl.innerText = `Final Score: ${score}`;

    leaderboardArea.style.display = 'block';
    scoresList.innerHTML = '';

    leaderboardData.forEach((player, index) => {
        const li = document.createElement('li');
        li.innerHTML = `
            <span>${index + 1}. ${player.name}</span>
            <span class="rank-score">${player.score}</span>
        `;
        scoresList.appendChild(li);
    });

    startBtn.innerText = 'REBOOT & RETRY';
}
