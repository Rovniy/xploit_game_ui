using UnityEngine;
using Xploit.GameUI;

/// <summary>
/// The scenario from the specification, wired by hand: the page emits "play"
/// when its button is clicked, the game answers by pushing health back into the
/// document, and the page can ask the game for the build version.
///
/// This is the whole C# side of a menu: no rendering code, no layout code.
/// </summary>
[RequireComponent(typeof(HtmlView))]
public sealed class MainMenuDemo : MonoBehaviour
{
    [Tooltip("Health the page starts from; each PLAY takes 20 off.")]
    [SerializeField] int m_health = 100;

    HtmlView m_view;

    void Awake()
    {
        m_view = GetComponent<HtmlView>();
    }

    void OnEnable()
    {
        m_view.On("play", OnPlay);
        m_view.RegisterFunction("getBuildInfo", _ => new System.Collections.Generic.Dictionary<string, object>
        {
            { "version", Application.version },
            { "platform", Application.platform.ToString() },
            { "unity", Application.unityVersion },
        });
    }

    void OnDisable()
    {
        m_view.Off("play", OnPlay);
        m_view.UnregisterFunction("getBuildInfo");
    }

    void OnPlay(WebEvent evt)
    {
        m_health = Mathf.Max(0, m_health - 20);
        Debug.Log($"[MainMenuDemo] PLAY pressed, health is now {m_health}");
        m_view.Send("healthChanged", m_health);
    }
}
