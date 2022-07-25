const url = location.origin.replace(/^http/, 'ws')+'/ws';
const socket = new WebSocket(url);

let id = 0;
const logEl = document.getElementById('log');
const msgEl = document.getElementById('msg');
const formEl = document.getElementById('form');

createMessage = (id, type, body) => JSON.stringify({id, type, body});

socket.onopen = () => {
    console.log('socket open');
}

socket.onclose = () => {
    console.log('socket closed');
}

socket.onerror = (e) => {
    console.log('socket error', e);
}

socket.onmessage = (msg) => {
    const message = JSON.parse(msg.data);
    switch(message.type){
        case 'chat':
            logEl.value = `${message.body}\n${logEl.value}`;
            break;
        default:
            console.error('Unknown message type');
    }
};

formEl.addEventListener('submit', (event) => {
    event.preventDefault();
    socket.send(createMessage(id++, 'chat', msgEl.value));
});