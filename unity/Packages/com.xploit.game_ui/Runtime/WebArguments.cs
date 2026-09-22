using System.Collections.Generic;

namespace Xploit.GameUI
{
    /// <summary>
    /// Arguments passed between JavaScript and C# (JSON array payload).
    /// Stage 1 only carries the shape; parsing/serialisation arrives in Stage 7.
    /// </summary>
    public sealed class WebArguments
    {
        readonly List<object> m_values;

        public WebArguments() : this(new List<object>()) { }

        internal WebArguments(List<object> values)
        {
            m_values = values ?? new List<object>();
        }

        public int Count => m_values.Count;

        public object this[int index] => m_values[index];

        public T Get<T>(int index) => (T)System.Convert.ChangeType(m_values[index], typeof(T));

        public static WebArguments FromObjects(params object[] values) => new WebArguments(new List<object>(values ?? new object[0]));
    }
}
