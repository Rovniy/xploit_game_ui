using System.Threading.Tasks;
using UnityEngine;
using Xploit.GameUI;

/// <summary>
/// The whole game side of an HTML HUD. There is no layout code, no drawing code
/// and no C++: the page owns how the HUD looks, this owns what it means.
///
/// Setup: a Canvas with a RawImage, an HtmlView and a WebInput on the same
/// GameObject, Document Path set to UI/HUD/index.html, and an EventSystem in the
/// scene. The Inspector warns about each of those if one is missing.
/// </summary>
[RequireComponent(typeof(HtmlView))]
public sealed class Hud : MonoBehaviour
{
    [SerializeField] int m_health = 100;
    [SerializeField] int m_ammo = 30;
    [SerializeField] int m_magazineSize = 30;
    [SerializeField] int m_spareMagazines = 3;
    [Tooltip("How long a reload takes, in seconds.")]
    [SerializeField] float m_reloadSeconds = 1.5f;

    HtmlView m_view;

    void Awake()
    {
        m_view = GetComponent<HtmlView>();
    }

    void OnEnable()
    {
        // Push the starting values as soon as the page's scripts have run: any
        // earlier and Unity.on would not be registered yet.
        m_view.JsReady += PushEverything;
        m_view.On("inventory", OnInventory);
        m_view.RegisterFunctionAsync("reload", Reload);
    }

    void OnDisable()
    {
        m_view.JsReady -= PushEverything;
        m_view.Off("inventory", OnInventory);
        m_view.UnregisterFunction("reload");
    }

    void PushEverything(HtmlView view)
    {
        view.Send("healthChanged", m_health);
        view.Send("ammoChanged", m_ammo);
    }

    void OnInventory(WebEvent evt)
    {
        Debug.Log("[Hud] the page asked to open the inventory");
    }

    async Task<object> Reload(WebArguments args)
    {
        if (m_spareMagazines <= 0)
        {
            // The message reaches the page as a rejected promise.
            throw new System.InvalidOperationException("no magazines left");
        }
        await Task.Delay(Mathf.RoundToInt(m_reloadSeconds * 1000f));

        var loaded = m_magazineSize - m_ammo;
        m_ammo = m_magazineSize;
        m_spareMagazines--;
        m_view.Send("ammoChanged", m_ammo);
        return loaded;
    }

    /// <summary>Call this from your damage code.</summary>
    public void ApplyDamage(int amount)
    {
        m_health = Mathf.Max(0, m_health - amount);
        m_view.Send("healthChanged", m_health);
    }

    /// <summary>Call this when a shot is fired.</summary>
    public void SpendRound()
    {
        m_ammo = Mathf.Max(0, m_ammo - 1);
        m_view.Send("ammoChanged", m_ammo);
    }
}
