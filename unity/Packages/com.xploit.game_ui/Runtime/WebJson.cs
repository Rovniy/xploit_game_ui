using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace Xploit.GameUI
{
    /// <summary>
    /// JSON for the bridge payloads. Small on purpose: the engine never looks
    /// inside a payload, so both sides only need to agree on JSON itself, and
    /// pulling in a JSON package would be a dependency for a few hundred lines.
    /// </summary>
    public static class WebJson
    {
        /// <summary>Serialises one value. Unsupported types fall back to JsonUtility.</summary>
        public static string Serialize(object value)
        {
            var builder = new StringBuilder(64);
            WriteValue(builder, value, 0);
            return builder.ToString();
        }

        /// <summary>
        /// Serialises an argument list as a JSON array, which is what
        /// Unity.on handlers receive as their arguments.
        /// </summary>
        public static string SerializeArguments(object[] values)
        {
            if (values == null || values.Length == 0)
            {
                return null; // no payload at all
            }
            var builder = new StringBuilder(64);
            builder.Append('[');
            for (int i = 0; i < values.Length; i++)
            {
                if (i > 0)
                {
                    builder.Append(',');
                }
                WriteValue(builder, values[i], 0);
            }
            builder.Append(']');
            return builder.ToString();
        }

        /// <summary>
        /// Parses JSON. Returns null when the text is not valid JSON, which the
        /// caller should treat as a payload it cannot use.
        /// </summary>
        public static WebValue Parse(string json)
        {
            if (string.IsNullOrEmpty(json))
            {
                return null;
            }
            var reader = new Reader(json);
            try
            {
                var value = reader.ReadValue();
                reader.SkipWhitespace();
                return reader.AtEnd ? value : null;
            }
            catch (FormatException)
            {
                return null;
            }
        }

        // ---- writing -------------------------------------------------------

        const int MaxDepth = 32;

        static void WriteValue(StringBuilder builder, object value, int depth)
        {
            if (depth > MaxDepth)
            {
                // Cycles would otherwise recurse for ever.
                builder.Append("null");
                return;
            }
            switch (value)
            {
                case null:
                    builder.Append("null");
                    return;
                case bool flag:
                    builder.Append(flag ? "true" : "false");
                    return;
                case string text:
                    WriteString(builder, text);
                    return;
                case char character:
                    WriteString(builder, character.ToString());
                    return;
                case Enum enumeration:
                    // The name is what page code wants to compare against.
                    WriteString(builder, enumeration.ToString());
                    return;
                case float number:
                    WriteNumber(builder, number);
                    return;
                case double number:
                    WriteNumber(builder, number);
                    return;
                case decimal number:
                    builder.Append(number.ToString(CultureInfo.InvariantCulture));
                    return;
                case sbyte or byte or short or ushort or int or uint or long:
                    builder.Append(Convert.ToInt64(value, CultureInfo.InvariantCulture)
                        .ToString(CultureInfo.InvariantCulture));
                    return;
                case ulong number:
                    builder.Append(number.ToString(CultureInfo.InvariantCulture));
                    return;
                case WebValue web:
                    builder.Append(web.ToJson());
                    return;
                case WebArguments arguments:
                    WriteArray(builder, arguments.ToArray(), depth);
                    return;
                case IDictionary dictionary:
                    WriteObject(builder, dictionary, depth);
                    return;
                case IEnumerable sequence:
                    WriteArray(builder, sequence, depth);
                    return;
            }

            // Anything else: let Unity serialise it as a leaf object. That covers
            // [Serializable] types and the built-in structs such as Vector3.
            try
            {
                var json = JsonUtility.ToJson(value);
                builder.Append(string.IsNullOrEmpty(json) ? "null" : json);
            }
            catch (Exception)
            {
                builder.Append("null");
            }
        }

        static void WriteArray(StringBuilder builder, IEnumerable sequence, int depth)
        {
            builder.Append('[');
            var first = true;
            foreach (var item in sequence)
            {
                if (!first)
                {
                    builder.Append(',');
                }
                first = false;
                WriteValue(builder, item, depth + 1);
            }
            builder.Append(']');
        }

        static void WriteObject(StringBuilder builder, IDictionary dictionary, int depth)
        {
            builder.Append('{');
            var first = true;
            foreach (DictionaryEntry entry in dictionary)
            {
                if (!first)
                {
                    builder.Append(',');
                }
                first = false;
                WriteString(builder, Convert.ToString(entry.Key, CultureInfo.InvariantCulture) ?? string.Empty);
                builder.Append(':');
                WriteValue(builder, entry.Value, depth + 1);
            }
            builder.Append('}');
        }

        static void WriteNumber(StringBuilder builder, double number)
        {
            if (double.IsNaN(number) || double.IsInfinity(number))
            {
                builder.Append("null"); // JSON has no NaN or Infinity
                return;
            }
            builder.Append(number.ToString("R", CultureInfo.InvariantCulture));
        }

        static void WriteString(StringBuilder builder, string text)
        {
            builder.Append('"');
            foreach (var c in text)
            {
                switch (c)
                {
                    case '"': builder.Append("\\\""); break;
                    case '\\': builder.Append("\\\\"); break;
                    case '\n': builder.Append("\\n"); break;
                    case '\r': builder.Append("\\r"); break;
                    case '\t': builder.Append("\\t"); break;
                    case '\b': builder.Append("\\b"); break;
                    case '\f': builder.Append("\\f"); break;
                    default:
                        if (c < ' ')
                        {
                            builder.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        }
                        else
                        {
                            builder.Append(c);
                        }
                        break;
                }
            }
            builder.Append('"');
        }

        // ---- reading -------------------------------------------------------

        struct Reader
        {
            readonly string m_text;
            int m_index;

            public Reader(string text)
            {
                m_text = text;
                m_index = 0;
            }

            public bool AtEnd => m_index >= m_text.Length;

            public void SkipWhitespace()
            {
                while (m_index < m_text.Length)
                {
                    var c = m_text[m_index];
                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                    {
                        return;
                    }
                    m_index++;
                }
            }

            public WebValue ReadValue()
            {
                SkipWhitespace();
                if (AtEnd)
                {
                    throw new FormatException("unexpected end of JSON");
                }
                switch (m_text[m_index])
                {
                    case '{': return ReadObject();
                    case '[': return ReadArray();
                    case '"': return WebValue.FromString(ReadString());
                    case 't': Expect("true"); return WebValue.FromBool(true);
                    case 'f': Expect("false"); return WebValue.FromBool(false);
                    case 'n': Expect("null"); return WebValue.Null;
                    default: return WebValue.FromNumber(ReadNumber());
                }
            }

            WebValue ReadObject()
            {
                m_index++; // '{'
                var members = new List<KeyValuePair<string, WebValue>>();
                SkipWhitespace();
                if (!AtEnd && m_text[m_index] == '}')
                {
                    m_index++;
                    return WebValue.FromObject(members);
                }
                while (true)
                {
                    SkipWhitespace();
                    var key = ReadString();
                    SkipWhitespace();
                    Consume(':');
                    members.Add(new KeyValuePair<string, WebValue>(key, ReadValue()));
                    SkipWhitespace();
                    if (AtEnd)
                    {
                        throw new FormatException("unterminated object");
                    }
                    if (m_text[m_index] == ',')
                    {
                        m_index++;
                        continue;
                    }
                    Consume('}');
                    return WebValue.FromObject(members);
                }
            }

            WebValue ReadArray()
            {
                m_index++; // '['
                var items = new List<WebValue>();
                SkipWhitespace();
                if (!AtEnd && m_text[m_index] == ']')
                {
                    m_index++;
                    return WebValue.FromArray(items);
                }
                while (true)
                {
                    items.Add(ReadValue());
                    SkipWhitespace();
                    if (AtEnd)
                    {
                        throw new FormatException("unterminated array");
                    }
                    if (m_text[m_index] == ',')
                    {
                        m_index++;
                        continue;
                    }
                    Consume(']');
                    return WebValue.FromArray(items);
                }
            }

            string ReadString()
            {
                Consume('"');
                var builder = new StringBuilder();
                while (true)
                {
                    if (AtEnd)
                    {
                        throw new FormatException("unterminated string");
                    }
                    var c = m_text[m_index++];
                    if (c == '"')
                    {
                        return builder.ToString();
                    }
                    if (c != '\\')
                    {
                        builder.Append(c);
                        continue;
                    }
                    if (AtEnd)
                    {
                        throw new FormatException("unterminated escape");
                    }
                    var escape = m_text[m_index++];
                    switch (escape)
                    {
                        case '"': builder.Append('"'); break;
                        case '\\': builder.Append('\\'); break;
                        case '/': builder.Append('/'); break;
                        case 'b': builder.Append('\b'); break;
                        case 'f': builder.Append('\f'); break;
                        case 'n': builder.Append('\n'); break;
                        case 'r': builder.Append('\r'); break;
                        case 't': builder.Append('\t'); break;
                        case 'u':
                            if (m_index + 4 > m_text.Length)
                            {
                                throw new FormatException("truncated \\u escape");
                            }
                            // Surrogate pairs come through as two escapes and are
                            // appended as they are, which is what UTF-16 needs.
                            builder.Append((char)ushort.Parse(m_text.Substring(m_index, 4),
                                NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                            m_index += 4;
                            break;
                        default:
                            throw new FormatException("unknown escape \\" + escape);
                    }
                }
            }

            double ReadNumber()
            {
                var start = m_index;
                if (!AtEnd && (m_text[m_index] == '-' || m_text[m_index] == '+'))
                {
                    m_index++;
                }
                while (!AtEnd)
                {
                    var c = m_text[m_index];
                    if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                    {
                        m_index++;
                        continue;
                    }
                    break;
                }
                var text = m_text.Substring(start, m_index - start);
                if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var number))
                {
                    throw new FormatException("not a number: " + text);
                }
                return number;
            }

            void Consume(char expected)
            {
                if (AtEnd || m_text[m_index] != expected)
                {
                    throw new FormatException("expected '" + expected + "'");
                }
                m_index++;
            }

            void Expect(string literal)
            {
                if (m_index + literal.Length > m_text.Length ||
                    string.CompareOrdinal(m_text, m_index, literal, 0, literal.Length) != 0)
                {
                    throw new FormatException("expected " + literal);
                }
                m_index += literal.Length;
            }
        }
    }
}
