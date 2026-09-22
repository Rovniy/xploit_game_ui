// The page half of the sandbox menu. Everything it knows about the game goes
// through the bridge: it emits "play" and listens for "healthChanged".
const playButton = document.getElementById("play");
const healthFill = document.getElementById("health-fill");
const healthValue = document.getElementById("health-value");

function setHealth(value) {
  const clamped = Math.max(0, Math.min(100, value));
  healthFill.style.width = clamped + "%";
  healthValue.textContent = String(clamped);
}

playButton.addEventListener("click", () => {
  Unity.emit("play");
});

Unity.on("healthChanged", setHealth);

// Ask the game about itself. Nothing breaks when no handler is registered: the
// promise rejects and the page simply says so.
Unity.call("getBuildInfo")
  .then((info) => console.log(`running on ${info.platform}, Unity ${info.unity}`))
  .catch((error) => console.log(`no build info: ${error.message}`));

console.log("MainMenu ready");
