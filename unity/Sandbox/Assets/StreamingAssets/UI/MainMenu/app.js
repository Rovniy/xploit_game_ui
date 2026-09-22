// Sandbox menu. Input and DOM events work from stage 6; the Unity bridge
// (Unity.emit / Unity.on) arrives in stage 7, so it is used only when present.
const playButton = document.getElementById("play");
const healthFill = document.getElementById("health-fill");
const healthValue = document.getElementById("health-value");
const bridge = typeof Unity !== "undefined" ? Unity : null;

function setHealth(value) {
  const clamped = Math.max(0, Math.min(100, value));
  healthFill.style.width = clamped + "%";
  healthValue.textContent = String(clamped);
}

playButton.addEventListener("click", () => {
  console.log("PLAY clicked");
  if (bridge) {
    bridge.emit("play");
  } else {
    // Until the bridge exists, show that input works by taking damage.
    setHealth(parseInt(healthValue.textContent, 10) - 20);
  }
});

if (bridge) {
  bridge.on("healthChanged", setHealth);
}

console.log("MainMenu ready");
