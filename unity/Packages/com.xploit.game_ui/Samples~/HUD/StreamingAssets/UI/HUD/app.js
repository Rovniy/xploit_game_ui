// The page side of the HUD. It knows nothing about the game beyond the four
// message names below.
const healthFill = document.getElementById("health-fill");
const healthValue = document.getElementById("health-value");
const ammoFill = document.getElementById("ammo-fill");
const ammoValue = document.getElementById("ammo-value");
const toast = document.getElementById("toast");

function setBar(fill, value, label, max) {
  const clamped = Math.max(0, Math.min(max, value));
  fill.style.width = (clamped / max) * 100 + "%";
  label.textContent = String(clamped);
}

Unity.on("healthChanged", (value) => setBar(healthFill, value, healthValue, 100));
Unity.on("ammoChanged", (value) => setBar(ammoFill, value, ammoValue, 30));

document.getElementById("inventory").addEventListener("click", () => Unity.emit("inventory"));

// A call waits for the game to answer, so the button can report the result.
document.getElementById("reload").addEventListener("click", async () => {
  toast.textContent = "reloading...";
  try {
    const rounds = await Unity.call("reload");
    toast.textContent = `+${rounds} rounds`;
  } catch (error) {
    toast.textContent = error.message;
  }
});
