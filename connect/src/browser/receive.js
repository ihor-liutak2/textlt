"use strict";

const localSend = document.documentElement.dataset.protocol === "localsend";
const encoder = new TextEncoder();
const message = document.querySelector("#message");
const textLimit = localSend ? 16384 : 65536;
const status = document.querySelector("#status");
const content = document.querySelector("#content");
const fileInput = document.querySelector("#file-input");
const buttons = [...document.querySelectorAll("button")];
let sending = false;

const showStatus = text => { status.textContent = text; };
const waiting = "Waiting for approval in TextLT…";

async function send(transfer) {
  if (sending) return;
  sending = true;
  buttons.forEach(button => { button.disabled = true; });
  try {
    await transfer();
  } catch (error) {
    showStatus(error.message);
  } finally {
    buttons.forEach(button => { button.disabled = false; });
    sending = false;
  }
}

async function localSendRequest(path, body, contentType) {
  const response = await fetch("/api/localsend/v2/" + path, {
    method: "POST",
    headers: { "Content-Type": contentType },
    body
  });
  const errors = {
    403: "The transfer was declined or expired.",
    409: "The receiver is busy. Try again when its current transfer finishes.",
    429: "Too many attempts. Try again later."
  };
  if (!response.ok) throw new Error(errors[response.status] ||
    "LocalSend transfer failed (HTTP " + response.status + ").");
  return response;
}

async function sendLocalSend(items, text) {
  const files = Object.fromEntries(items.map((file, index) => [String(index), {
    id: String(index),
    fileName: file.name,
    size: file.size,
    fileType: file.type || "application/octet-stream",
    preview: text
  }]));
  const info = {
    alias: "Web Browser",
    version: "2.2",
    deviceType: "web",
    fingerprint: "web-" + makeId(),
    port: 0,
    protocol: "https",
    download: false
  };
  showStatus(waiting);
  const prepared = await localSendRequest("prepare-upload", JSON.stringify({info, files}), "application/json");
  if (prepared.status === 204) return text === undefined ? 0 : 1;
  const {sessionId, files: tokens} = await prepared.json();
  const accepted = Object.entries(tokens);
  try {
    let delivered = 0;
    for (const [fileId, token] of accepted) {
      showStatus("Uploading file " + (delivered + 1) + " of " + accepted.length + "…");
      const query = new URLSearchParams({sessionId, fileId, token});
      await localSendRequest("upload?" + query, items[Number(fileId)], "application/octet-stream");
      delivered++;
    }
    return delivered;
  } catch (error) {
    await localSendRequest("cancel?" + new URLSearchParams({sessionId}), null, "application/json").catch(() => {});
    throw error;
  }
}

function makeId() {
  return Array.from(crypto.getRandomValues(new Uint8Array(16)), byte =>
    byte.toString(16).padStart(2, "0")).join("");
}

function expectReply(socket, expected, data) {
  return new Promise((resolve, reject) => {
    function cleanup() {
      socket.removeEventListener("message", receive);
      socket.removeEventListener("close", disconnected);
      socket.removeEventListener("error", disconnected);
    }
    function receive(event) {
      cleanup();
      const reply = new Uint8Array(event.data);
      const errors = {
        2: "The receiver does not support this transfer type.",
        4: expected === 1 ? "The receiver is busy." : "The transfer was declined.",
        6: "The receiver rejected the text.",
        7: "TextLT could not save the file."
      };
      if (reply.length === 1 && reply[0] === expected) resolve();
      else reject(new Error(errors[reply[0]] || "Unexpected reply from TextLT."));
    }
    function disconnected() {
      cleanup();
      reject(new Error("The transfer connection closed or expired."));
    }
    socket.addEventListener("message", receive);
    socket.addEventListener("close", disconnected);
    socket.addEventListener("error", disconnected);
    try {
      data();
    } catch (error) {
      cleanup();
      reject(error);
    }
  });
}

async function sendTextLT(payload, name, itemLabel = "") {
  const file = name !== undefined;
  const id = encoder.encode(makeId());
  const filename = file ? encoder.encode(name) : new Uint8Array();
  if (file && filename.length > 255) throw new Error("File names must fit in 255 UTF-8 bytes.");
  const header = new Uint8Array((file ? 11 : 5) + id.length + filename.length);
  const view = new DataView(header.buffer);
  if (file) {
    header.set([1, id.length, filename.length]);
    view.setBigUint64(3, BigInt(payload.size));
  } else {
    header[0] = id.length;
    view.setUint32(1, payload.length);
  }
  header.set(id, file ? 11 : 5);
  header.set(filename, file ? 11 + id.length : header.length);

  const socket = new WebSocket("wss://" + location.host + "/transfer");
  socket.binaryType = "arraybuffer";
  try {
    await expectReply(socket, 1, () => {
      socket.addEventListener("open", () => socket.send(new Uint8Array([84, 76, 84, 67, file ? 2 : 1])),
        {once: true});
    });
    showStatus(waiting + itemLabel);
    await expectReply(socket, 3, () => socket.send(header));
    showStatus(file ? "Uploading…" + itemLabel : "Sending text…");
    const finished = expectReply(socket, 5, () => {
      if (!file && payload.length) socket.send(payload);
    });
    if (!file) return await finished;

    // Keep buffering bounded while streaming the file in native protocol chunks.
    const upload = async () => {
      const chunkSize = 65536;
      for (let offset = 0; offset < payload.size; offset += chunkSize) {
        while (socket.bufferedAmount >= 4 * chunkSize && socket.readyState === WebSocket.OPEN) {
          await new Promise(resolve => setTimeout(resolve, 20));
        }
        if (socket.readyState !== WebSocket.OPEN) throw new Error("The file upload was interrupted.");
        socket.send(payload.slice(offset, offset + chunkSize));
      }
      socket.send(new Uint8Array([8]));
      await finished;
    };
    await Promise.race([finished, upload()]);
  } finally {
    socket.close();
  }
}

document.querySelector("#text-form").addEventListener("submit", event => {
  event.preventDefault();
  const text = message.value;
  const bytes = encoder.encode(text);
  if (!bytes.length) return;
  send(async () => {
    if (bytes.length > textLimit) throw new Error("Text exceeds the " + textLimit + "-byte limit. Send it as a file.");
    if (localSend) await sendLocalSend([{name: "message.txt", size: bytes.length, type: "text/plain"}], text);
    else await sendTextLT(bytes);
    showStatus("Text delivered.");
  });
});

function sendFiles(files) {
  if (!files.length) return;
  send(async () => {
    let delivered;
    if (localSend) {
      if (files.length > 128) throw new Error("Choose up to 128 files at a time.");
      delivered = await sendLocalSend(files);
    } else {
      for (const [index, file] of files.entries()) {
        await sendTextLT(file, file.name, " (" + (index + 1) + "/" + files.length + ")");
      }
      delivered = files.length;
    }
    showStatus("Delivered " + delivered + " of " + files.length + " " + (files.length === 1 ? "file." : "files."));
  });
}

document.querySelector("#choose-files").addEventListener("click", () => fileInput.click());
fileInput.addEventListener("change", () => {
  sendFiles([...fileInput.files]);
  fileInput.value = "";
});

let dragDepth = 0;
const isFileDrag = event => event.dataTransfer && [...event.dataTransfer.types].includes("Files");
document.addEventListener("dragenter", event => {
  if (!isFileDrag(event)) return;
  event.preventDefault();
  if (++dragDepth === 1 && !sending) content.classList.add("drag-over");
});
document.addEventListener("dragover", event => {
  if (!isFileDrag(event)) return;
  event.preventDefault();
  event.dataTransfer.dropEffect = sending ? "none" : "copy";
});
document.addEventListener("dragleave", () => {
  if (dragDepth && --dragDepth === 0) content.classList.remove("drag-over");
});
document.addEventListener("drop", event => {
  if (!isFileDrag(event)) return;
  event.preventDefault();
  dragDepth = 0;
  content.classList.remove("drag-over");
  const items = [...event.dataTransfer.items];
  const files = items.length ? items.filter(item => !item.webkitGetAsEntry?.()?.isDirectory)
    .map(item => item.getAsFile()).filter(Boolean) : [...event.dataTransfer.files];
  sendFiles(files);
});
