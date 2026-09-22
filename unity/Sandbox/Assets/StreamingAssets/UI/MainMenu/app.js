// Stage 7 target: JS -> Unity (emit) and Unity -> JS (on).
const playButton = document.getElementById("play");
const healthFill = document.getElementById("health-fill");
const healthValue = document.getElementById("health-value");

playButton.addEventListener("click", () => {
  Unity.emit("play");
});

Unity.on("healthChanged", (value) => {
  const clamped = Math.max(0, Math.min(100, value));
  healthFill.style.width = clamped + "%";
  healthValue.textContent = String(clamped);
});

console.log("MainMenu ready");
