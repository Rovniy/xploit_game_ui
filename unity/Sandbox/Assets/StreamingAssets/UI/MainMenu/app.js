// The page half of the sandbox menu. Everything it knows about the game goes
// through the bridge: it emits "play" and listens for "healthChanged".
const playButton = document.getElementById("play");
const healthFill = document.getElementById("health-fill");
const healthValue = document.getElementById("health-value");
const nameField = document.getElementById("player-name");
const log = document.getElementById("log");

function addLine(text) {
  const line = document.createElement("div");
  line.className = "line fresh";
  line.textContent = text;
  log.appendChild(line);
  // Keep the newest line in sight.
  log.scrollTop = log.scrollHeight;
  // The highlight belongs to the line that just arrived, not the ones before it.
  setTimeout(() => line.classList.remove("fresh"), 400);
}

function setHealth(value) {
  const clamped = Math.max(0, Math.min(100, value));
  healthFill.style.width = clamped + "%";
  healthValue.textContent = String(clamped);
  addLine(`health ${clamped}`);
}

playButton.addEventListener("click", () => {
  Unity.emit("play", nameField.value);
});

nameField.addEventListener("change", () => {
  addLine(`name is now ${nameField.value}`);
});

Unity.on("healthChanged", setHealth);

// Ask the game about itself. Nothing breaks when no handler is registered: the
// promise rejects and the page simply says so.
Unity.call("getBuildInfo")
  .then((info) => addLine(`${info.platform}, Unity ${info.unity}`))
  .catch((error) => addLine(error.message));

addLine("ready");
