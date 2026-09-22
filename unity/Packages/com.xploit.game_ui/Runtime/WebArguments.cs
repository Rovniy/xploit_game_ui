using System.Collections;
using System.Collections.Generic;

namespace Xploit.GameUI
{
    /// <summary>
    /// The arguments of one bridge message: what page script passed to
    /// Unity.emit or Unity.call, or what the game passed to
    /// <see cref="HtmlView.Send"/>.
    ///
    /// Indexing past the end returns <see cref="WebValue.Null"/> rather than
    /// throwing, because the page is free to send fewer arguments than the
    /// handler reads.
    /// </summary>
    public sealed class WebArguments : IEnumerable<WebValue>
    {
        static readonly List<WebValue> Empty = new List<WebValue>();

        readonly List<WebValue> m_values;

        public static readonly WebArguments None = new WebArguments(Empty);

        internal WebArguments(List<WebValue> values)
        {
            m_values = values ?? Empty;
        }

        /// <summary>Builds the arguments from a parsed JSON array payload.</summary>
        internal static WebArguments FromJson(string json)
        {
            if (string.IsNullOrEmpty(json))
            {
                return None;
            }
            var parsed = WebJson.Parse(json);
            if (parsed == null)
            {
                return None;
            }
            if (!parsed.IsArray)
            {
                return new WebArguments(new List<WebValue> { parsed });
            }
            return new WebArguments(new List<WebValue>(parsed.Items));
        }

        public int Count => m_values.Count;

        public WebValue this[int index] =>
            index >= 0 && index < m_values.Count ? m_values[index] : WebValue.Null;

        /// <summary>Argument <paramref name="index"/> as <typeparamref name="T"/>.</summary>
        public T Get<T>(int index) => this[index].Get<T>();

        /// <summary>Argument <paramref name="index"/> mapped onto a [Serializable] type.</summary>
        public T GetObject<T>(int index) => this[index].GetObject<T>();

        public string GetString(int index) => this[index].AsString;
        public double GetNumber(int index) => this[index].AsNumber;
        public int GetInt(int index) => (int)this[index].AsNumber;
        public bool GetBool(int index) => this[index].AsBool;

        /// <summary>The arguments as plain C# values (null, bool, double, string, List, Dictionary).</summary>
        public object[] ToArray()
        {
            var result = new object[m_values.Count];
            for (int i = 0; i < m_values.Count; i++)
            {
                result[i] = m_values[i].ToObject();
            }
            return result;
        }

        public IEnumerator<WebValue> GetEnumerator() => m_values.GetEnumerator();

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        public override string ToString()
        {
            var parts = new List<string>(m_values.Count);
            foreach (var value in m_values)
            {
                parts.Add(value.ToJson());
            }
            return "[" + string.Join(", ", parts) + "]";
        }
    }
}
