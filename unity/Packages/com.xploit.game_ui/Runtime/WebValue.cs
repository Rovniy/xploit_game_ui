using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace Xploit.GameUI
{
    /// <summary>
    /// One value that came from page script. A tree rather than a C# object, so
    /// nothing is lost when the page sends a shape the game did not declare;
    /// <see cref="GetObject{T}"/> maps it onto a type when you have one.
    /// </summary>
    public sealed class WebValue
    {
        public enum Kind
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object,
        }

        static readonly List<WebValue> EmptyItems = new List<WebValue>();
        static readonly List<KeyValuePair<string, WebValue>> EmptyMembers =
            new List<KeyValuePair<string, WebValue>>();

        readonly bool m_bool;
        readonly double m_number;
        readonly string m_string;
        readonly List<WebValue> m_items;
        readonly List<KeyValuePair<string, WebValue>> m_members;

        public static readonly WebValue Null = new WebValue(Kind.Null, false, 0, null, null, null);

        WebValue(Kind type, bool flag, double number, string text, List<WebValue> items,
            List<KeyValuePair<string, WebValue>> members)
        {
            Type = type;
            m_bool = flag;
            m_number = number;
            m_string = text;
            m_items = items;
            m_members = members;
        }

        internal static WebValue FromBool(bool value) => new WebValue(Kind.Bool, value, 0, null, null, null);
        internal static WebValue FromNumber(double value) => new WebValue(Kind.Number, false, value, null, null, null);
        internal static WebValue FromString(string value) => new WebValue(Kind.String, false, 0, value, null, null);
        internal static WebValue FromArray(List<WebValue> items) =>
            new WebValue(Kind.Array, false, 0, null, items, null);
        internal static WebValue FromObject(List<KeyValuePair<string, WebValue>> members) =>
            new WebValue(Kind.Object, false, 0, null, null, members);

        public Kind Type { get; }

        public bool IsNull => Type == Kind.Null;
        public bool IsArray => Type == Kind.Array;
        public bool IsObject => Type == Kind.Object;

        public bool AsBool => Type switch
        {
            Kind.Bool => m_bool,
            Kind.Number => m_number != 0,
            Kind.String => !string.IsNullOrEmpty(m_string),
            Kind.Null => false,
            _ => true,
        };

        public double AsNumber => Type switch
        {
            Kind.Number => m_number,
            Kind.Bool => m_bool ? 1 : 0,
            Kind.String => double.TryParse(m_string, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed)
                ? parsed
                : 0,
            _ => 0,
        };

        /// <summary>The string form: the text itself for strings, JSON otherwise.</summary>
        public string AsString => Type switch
        {
            Kind.String => m_string,
            Kind.Null => null,
            Kind.Bool => m_bool ? "true" : "false",
            Kind.Number => m_number.ToString("R", CultureInfo.InvariantCulture),
            _ => ToJson(),
        };

        /// <summary>Element count for an array, member count for an object, else 0.</summary>
        public int Count => Type == Kind.Array ? m_items.Count : Type == Kind.Object ? m_members.Count : 0;

        /// <summary>Array element; <see cref="Null"/> when the index is out of range.</summary>
        public WebValue this[int index]
        {
            get
            {
                var items = m_items ?? EmptyItems;
                return index >= 0 && index < items.Count ? items[index] : Null;
            }
        }

        /// <summary>Object member; <see cref="Null"/> when it is missing.</summary>
        public WebValue this[string key]
        {
            get
            {
                foreach (var member in m_members ?? EmptyMembers)
                {
                    if (member.Key == key)
                    {
                        return member.Value;
                    }
                }
                return Null;
            }
        }

        public bool Has(string key) => !ReferenceEquals(this[key], Null);

        public IEnumerable<string> Keys
        {
            get
            {
                foreach (var member in m_members ?? EmptyMembers)
                {
                    yield return member.Key;
                }
            }
        }

        public IEnumerable<WebValue> Items => m_items ?? EmptyItems;

        /// <summary>
        /// Converts to a primitive, a string or an enum. Anything else goes
        /// through <see cref="GetObject{T}"/>.
        /// </summary>
        public T Get<T>()
        {
            var target = typeof(T);
            if (target == typeof(string))
            {
                return (T)(object)AsString;
            }
            if (target == typeof(bool))
            {
                return (T)(object)AsBool;
            }
            if (target.IsEnum)
            {
                // The serialiser writes enums by name, so read them back by name
                // and still accept a number.
                if (Type == Kind.String && Enum.IsDefined(target, m_string))
                {
                    return (T)Enum.Parse(target, m_string);
                }
                return (T)Enum.ToObject(target, (long)AsNumber);
            }
            if (target == typeof(WebValue))
            {
                return (T)(object)this;
            }
            if (target.IsPrimitive || target == typeof(decimal))
            {
                return (T)Convert.ChangeType(AsNumber, target, CultureInfo.InvariantCulture);
            }
            return GetObject<T>();
        }

        /// <summary>
        /// Maps an object value onto a [Serializable] type through JsonUtility.
        /// Returns the default value when the shape does not fit.
        /// </summary>
        public T GetObject<T>()
        {
            if (Type != Kind.Object && Type != Kind.Array)
            {
                return default;
            }
            try
            {
                return JsonUtility.FromJson<T>(ToJson());
            }
            catch (Exception)
            {
                return default;
            }
        }

        /// <summary>A plain C# tree: null, bool, double, string, List or Dictionary.</summary>
        public object ToObject()
        {
            switch (Type)
            {
                case Kind.Bool: return m_bool;
                case Kind.Number: return m_number;
                case Kind.String: return m_string;
                case Kind.Array:
                {
                    var list = new List<object>(m_items.Count);
                    foreach (var item in m_items)
                    {
                        list.Add(item.ToObject());
                    }
                    return list;
                }
                case Kind.Object:
                {
                    var map = new Dictionary<string, object>(m_members.Count);
                    foreach (var member in m_members)
                    {
                        map[member.Key] = member.Value.ToObject();
                    }
                    return map;
                }
                default: return null;
            }
        }

        /// <summary>The value back as JSON.</summary>
        public string ToJson()
        {
            var builder = new StringBuilder(32);
            Write(builder);
            return builder.ToString();
        }

        public override string ToString() => ToJson();

        void Write(StringBuilder builder)
        {
            switch (Type)
            {
                case Kind.Null:
                    builder.Append("null");
                    break;
                case Kind.Bool:
                    builder.Append(m_bool ? "true" : "false");
                    break;
                case Kind.Number:
                    builder.Append(m_number.ToString("R", CultureInfo.InvariantCulture));
                    break;
                case Kind.String:
                    builder.Append(WebJson.Serialize(m_string));
                    break;
                case Kind.Array:
                    builder.Append('[');
                    for (int i = 0; i < m_items.Count; i++)
                    {
                        if (i > 0)
                        {
                            builder.Append(',');
                        }
                        m_items[i].Write(builder);
                    }
                    builder.Append(']');
                    break;
                case Kind.Object:
                    builder.Append('{');
                    for (int i = 0; i < m_members.Count; i++)
                    {
                        if (i > 0)
                        {
                            builder.Append(',');
                        }
                        builder.Append(WebJson.Serialize(m_members[i].Key)).Append(':');
                        m_members[i].Value.Write(builder);
                    }
                    builder.Append('}');
                    break;
            }
        }
    }
}
