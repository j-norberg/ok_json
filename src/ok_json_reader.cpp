#include "ok_json_reader.h"

#include <cmath>
#include <vector>
#include <string>

// fixme utf8? (other encoding too?)
	
const uint64_t k_fnv1a_mul = 0x00000100000001B3UL;

namespace OkJsonReader_Private
{
	using namespace OkJsonReader;

	enum
	{
		k_power_table_size = 20
	};

	double k_power_table[k_power_table_size] = {
	1.0,
	10.0,
	100.0,
	1000.0,
	10000.0,
	100000.0,
	1000000.0,
	10000000.0,
	100000000.0,
	1000000000.0,
	10000000000.0,
	100000000000.0,
	1000000000000.0,
	10000000000000.0,
	100000000000000.0,
	1000000000000000.0,
	10000000000000000.0,
	100000000000000000.0,
	1000000000000000000.0,
	10000000000000000000.0,
	};

	struct ObjectStackElement
	{
		std::vector<KvP> _object_kvps;
	};

	struct ArrayStackElement
	{
		std::vector<Value> _array_values;
	};

	bool calculate_line_col(TextSpan full_text, TextSpan read, int& line, int& col)
	{
		const char* s = read._b;

		// find line, col
		col = 1;
		line = 1;
		while (s > full_text._b)
		{
			--s;
			if (*s == '\n')
			{
				++line;
				break;
			}
			++col;
		}

		while (s > full_text._b)
		{
			--s;
			if (*s == '\n')
				++line;
		}

		return true;
	}

	void line_and_col_to_string(TextSpan full_text, TextSpan read, std::string& dst, const char* desc)
	{
		int line = 0;
		int col = 0;
		calculate_line_col(full_text, read, line, col);
		dst = "line: ";
		dst += std::to_string(line);
		dst += ", col: ";
		dst += std::to_string(col);
		dst += " desc: ";
		dst += desc;
	}

	bool is_ws(char c)
	{
		// fixme be more diligent with bad data
		// like non-text input?
		switch (c)
		{
		case ' ':
		case '\t':
		case '\n':
		case '\r':
		case '\f':
			return true;
		}
		return false;
	}

	struct Parser
	{
		TextSpan _read; // read from here

		Parsed* _dest = nullptr;

		int _parse_depth = 0;

		// error into
		bool _error = false;
		std::string _error_description;

		void skip_ws()
		{
			for (; _read._b < _read._e; ++_read._b)
			{
				int v = *_read._b;
				if (!is_ws(v))
				{
					if (v == '/')
					{
						skip_comment(); // skip until eol, and then keep going (not technically json spec. but very useful)
					}
					else
					{
						break;
					}
				}
			}
		}

		// loop until "
		void skip_string()
		{
			// fixme escape codes and utf8
			for (; _read._b < _read._e; ++_read._b)
			{
				if (*_read._b == '\"')
				{
					// only break if not part of escape code
					if (_read._b[-1] != '\\')
						break;
				}
			}
		}

		// same as skip_string but also calculate hash
		// loop until "
		uint64_t skip_key()
		{
			uint64_t h = k_fnv1a_offset_basis;

			for (; _read._b < _read._e; ++_read._b)
			{
				int v = *_read._b;
				if (v == '\"')
				{
					// do not break if part of escape code
					if (_read._b[-1] != '\\')
						break;
				}

				h ^= v;
				h *= k_fnv1a_mul;
			}

			return h;
		}

		double accept_fraction()
		{
			double weight = 1;
			double v = 0;
			for (; _read._b < _read._e; ++_read._b)
			{
				char c = *_read._b;
				switch (c)
				{
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					weight *= 10;
					v *= 10;
					v += (c - '0');
					break;
				default:
					return v / weight;
				}
			}
			return v / weight;
		}

		// loop until not inside number
		int64_t accept_digits()
		{
			int64_t v = 0;

			for (; _read._b < _read._e; ++_read._b)
			{
				char c = *_read._b;
				switch (c)
				{
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					v *= 10;
					v += c - '0';
					break;
				default:
					return v;
				}
			}
			return v;
		}

		Key parse_key()
		{
			// ensure "
			if (!accept('\"'))
			{
				_error = true;
				line_and_col_to_string(_dest->_text, _read, _error_description, "key needs to start with \"");
				return { -1, -1, 0 };
			}

			// set start
			int key_start = (int)(_read._b - _dest->_text._b);

			// loop until "
			uint64_t key_hash = skip_key();
			if (!accept('\"'))
			{
				_error = true;
				line_and_col_to_string(_dest->_text, _read, _error_description, "key needs to end with \"");
				return { -1, -1, 0 };
			}

			// set end
			int key_end = (int)(_read._b-1 - _dest->_text._b);

			return { key_start, key_end, key_hash };
		}

		Value parse_object()
		{
			++_parse_depth;

			// fixme push to object-stack
			ObjectStackElement o;

			++_read._b; // skip '{'
			for ( ; _read._b < _read._e ; )
			{
				skip_ws();

				// empty object? (also allows for trailing comma)
				if (accept('}'))
					break;

				Key k = parse_key(); // does not touch the source-text, thus leaves escape codes
				if (_error)
				{
					return { e_null, -1, -1, 0 };
				}

				skip_ws();
				if (!accept(':'))
				{
					_error = true;
					line_and_col_to_string(_dest->_text, _read, _error_description, "need \":\" after key");
					return { e_null, -1, -1, 0 };
				}

				// expect :
				Value v = parse_value();
				if (_error)
				{
					return { e_null, -1, -1, 0 };
				}

				KvP kvp{k,v};
				o._object_kvps.push_back(kvp);

				skip_ws();
				if (accept('}'))
					break;

				if (!accept(','))
				{
					_error = true;
					line_and_col_to_string(_dest->_text, _read, _error_description, "need \",\" between key-values");
					return { e_null, -1, -1, 0 }; // null-value is error...
				};
			}

			// copy kvp from stack to "parsed"
			int object_begin = (int)_dest->_object_kvps.size();
			_dest->_object_kvps.insert(_dest->_object_kvps.end(), o._object_kvps.begin(), o._object_kvps.end());
			int object_end = (int)_dest->_object_kvps.size();

			// fixme pop from object-stack
			--_parse_depth;

			// return the value
			return { e_object, object_begin, object_end, 0 }; // null-value is error...
		}

		
		Value parse_array()
		{
			++_parse_depth;

			// fixme push to array-stack
			ArrayStackElement a;

			++_read._b; // skip '['

			for (; _read._b < _read._e; )
			{
				skip_ws();

				// accept empty array?
				if (accept(']'))
					break;

				Value v = parse_value();
				if (_error)
				{
					return { e_null, -1, -1, 0 };
				}

				a._array_values.push_back(v);

				skip_ws();

				// last element?
				if (accept(']'))
					break;

				if (!accept(','))
				{
					_error = true;
					line_and_col_to_string(_dest->_text, _read, _error_description, "need \",\" between values");
					return { e_null, -1, -1, 0 }; // null-value is error...
				}
			}

			// copy kvp from stack to "parsed"
			int array_begin = (int)_dest->_array_values.size();
			_dest->_array_values.insert(_dest->_array_values.end(), a._array_values.begin(), a._array_values.end());
			int array_end = (int)_dest->_array_values.size();

			// fixme pop from object-stack
			--_parse_depth;

			// return the value
			return { e_array, array_begin, array_end, 0 }; // null-value is error...
		}

		void skip_comment()
		{
			// skip past '/'
			++_read._b;

			if (!accept('/'))
			{
				_error = true;
				line_and_col_to_string(_dest->_text, _read, _error_description, "comment starts with //");
				return;
			}

			// find newline
			for (; _read._b < _read._e; ++_read._b)
			{
				int v = *_read._b;
				if ( v == '\n' || v == '\r' )
				{
					break;
				}
			}
		}

		Value parse_string()
		{
			// skip past '"'
			++_read._b;

			// set start
			int string_start = (int)(_read._b - _dest->_text._b);

			// loop until "
			skip_string();
			if (!accept('\"'))
			{
				_error = true;
				line_and_col_to_string(_dest->_text, _read, _error_description, "string needs to end with \"");
				return { e_null, -1, -1, 0 };
			}

			// set end
			int string_end = (int)(_read._b - 1 - _dest->_text._b);

			return { e_string, string_start, string_end, 0 };
		}

		Value parse_true()
		{
			// skip past 't'
			++_read._b;

			if (
				_read._b[0] != 'r' ||
				_read._b[1] != 'u' ||
				_read._b[2] != 'e'
				)
			{
				_error = true;
				line_and_col_to_string(_dest->_text, _read, _error_description, "invalid value, expecting \"true\"");
				return { e_null, -1, -1, 0.0 };
			}

			_read._b += 3;

			int true_end = (int)(_read._b - _dest->_text._b);
			return { e_true, true_end - 4, true_end, 0 };
		}

		Value parse_false()
		{
			// skip past 'f'
			++_read._b;

			if (
				_read._b[0] != 'a' ||
				_read._b[1] != 'l' ||
				_read._b[2] != 's' ||
				_read._b[3] != 'e'
				)
			{
				_error = true;
				line_and_col_to_string(_dest->_text, _read, _error_description, "invalid value, expecting \"false\"");
				return { e_null, -1, -1, 0 };
			}

			_read._b += 4;

			int false_end = (int)(_read._b - _dest->_text._b);
			return { e_false, false_end - 5, false_end, 0 };
		}

		inline bool accept(char v1, char v2)
		{
			int v = *_read._b;
			bool r = (v == v1) || (v == v2);
			if (r)
			{
				++_read._b;
			}
			return r;
		}

		inline bool accept(char v)
		{
			bool r = *_read._b == v;
			if (r)
			{
				++_read._b;
			}
			return r;
		}

		Value parse_number()
		{
			int number_start = (int)(_read._b - _dest->_text._b);

			Type number_type = e_int; // a convenience...
			int64_t whole = 0;
			double fraction = 0;

			bool flip_sign = accept('-');
			bool leading_zero = accept('0');
			if (!leading_zero)
			{
				// digits until 
				whole = accept_digits();
			}

			bool has_fraction = accept('.');
			if (has_fraction)
			{
				// fixme this could be better if we did the exponent-shifting while parsing these (since it could end up as a large number)
				// digits
				fraction = accept_fraction();
				if (fraction != 0.0)
				{
					number_type = e_number;
				}
			}

			double v = whole + fraction;

			bool has_exponent = accept('e','E');
			if (has_exponent)
			{
				// +/-
				bool flip_exponent_sign = accept('-');
				bool dummy = accept('+');
				(void)dummy; // unused

				// digits again
				int64_t exponent = accept_digits();
				if (exponent != 0.0)
				{
					double mul;
					if (exponent < k_power_table_size)
					{
						mul = k_power_table[exponent];
					}
					else
					{
						number_type = e_number;
						mul = pow(10.0, (double)exponent);
					}

					if (flip_exponent_sign)
					{
						number_type = e_number;
						v /= mul;
					}
					else
					{
						v *= mul;
					}
				}
			}

			if (flip_sign)
			{
				v = -v;
			}

			int number_end = (int)(_read._b - _dest->_text._b);
			return { number_type, number_start, number_end, v };
		}

		Value parse_null()
		{
			// skip past 'n'
			++_read._b;

			if (
				_read._b[0] != 'u' ||
				_read._b[1] != 'l' ||
				_read._b[2] != 'l'
				)
			{
				_error = true;
				line_and_col_to_string(_dest->_text, _read, _error_description, "invalid value, expecting \"null\"");
				return { e_null, -1, -1, 0.0 };
			}

			_read._b += 3;

			int null_end = (int)(_read._b - _dest->_text._b);
			return { e_null, null_end - 4, null_end, 0 };
		}

		Value parse_value()
		{
			skip_ws();

			// find non-ws
			switch (*_read._b)
			{
			case '{': return parse_object();
			case '[': return parse_array();
			case 't': return parse_true();
			case 'f': return parse_false();
			case 'n': return parse_null();
			case '\"': return parse_string();

			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case '-': // a number can start with -
				return parse_number();
			}

			// error
			// expecting a value got "character"
			_error = true;
			line_and_col_to_string(_dest->_text, _read, _error_description, "expecting value");
			return { e_null, -1, -1, 0 }; // null-value is error...
		}

		// can scan and count the numbers of { and } to guess the sizes
		void parse(TextSpan text, Parsed* dest)
		{
			_dest = dest;

			_dest->_text = text;
			_read = text;

			_dest->_root = parse_value();

			// at this point we really expect EOF
			skip_ws();

			if (_read._b < _read._e)
			{
				_error = true;
				line_and_col_to_string(_dest->_text, _read, _error_description, "expecting EOF");
			}
		}
	};
}



namespace OkJsonReader
{
	const TextSpan k_array_str("array can not be viewed as string");
	const TextSpan k_object_str("object can not be viewed as string");
	const TextSpan k_null_str("null can not be viewed as string");

	using namespace OkJsonReader_Private;

	HashedKeyStripped HashedKeyStripped::from_string(const char* text)
	{
		int i = 0;
		uint64_t h = k_fnv1a_offset_basis;

		for (; ; ++i)
		{
			int v = text[i];
			if (v == 0)
				break;

			h ^= v;
			h *= k_fnv1a_mul;
		}

		HashedKeyStripped ret;
		ret._s = i;
		ret._h = h;
		return ret;
	}

	HashedKey::HashedKey(const char* text)
	{
		_b = text;
		
		int i = 0;
		uint64_t h = k_fnv1a_offset_basis;

		for (; ; ++i)
		{
			int v = text[i];
			if (v == 0)
				break;

			h ^= v;
			h *= k_fnv1a_mul;
		}

		_s = i;
		_h = h;
	};

//	static std::string unescape(TextSpan text); // applies escape-codes

	// Value Proxy
	Proxy::Proxy(Value value, const Parsed* parsed)
		: _value(value)
		, _parsed(parsed)
	{
	};

	Type Proxy::debug_get_type() const
	{
		return _value._t;
	}

	TextSpan Proxy::debug_get_as_raw_string() const // still has escape characters
	{
		switch (_value._t)
		{
		case e_array: return k_array_str;
		case e_object: return k_object_str;
		case e_null: return k_null_str;
		default:break;
		}

		const char* text = _parsed->_text._b;
		return TextSpan( text + _value._b, text + _value._e );
	}

#if 0
	escape
		'"'
		'\\'
		'/'
		'b'
		'f'
		'n'
		'r'
		't'
		'u' hex hex hex hex
#endif

	std::string Proxy::unescape(TextSpan text)
	{
		// copy and undo escape code
		std::string r;
		ptrdiff_t lim = text._e - text._b;
		r.reserve(lim);

		const char* b = text._b;
		for (ptrdiff_t i = 0; i < lim; ++i)
		{
			int v = b[i];
			if (v == '\\')
			{
				// escape
				++i;
				if (i >= lim)
				{
					return r;
				}
				v = b[i];
				switch (v)
				{
				case '\"': break;
				case '\\': break;
				case '/': break;

				case 'b': v = '\b'; break;
				case 'f': v = '\f'; break;
				case 'n': v = '\n'; break;
				case 'r': v = '\r'; break;
				case 't': v = '\t'; break;
				case 'u': break; // fixme

				default:
					// bug?
					return r;
					break;
				}
			}

			r.push_back(v);
		}

		return r;
	}

	// simplest getters (valid)
	bool Proxy::try_get(TextSpan& v) const
	{
		switch (_value._t)
		{
		case e_string:
			{
			const char* text = _parsed->_text._b;
			v._b = text + _value._b;
			v._e = text + _value._e;
			}
			return true;
		
		default: break;
		}

		return false;
	}

	bool Proxy::try_get(std::string& v) const
	{
		switch (_value._t)
		{
		case e_string:
			{
			const char* text = _parsed->_text._b;
			v.assign(text + _value._b, text + _value._e);
			}
			return true;

		default:break;
		}

		return false;
	}

	bool Proxy::try_get(bool& v) const
	{
		switch (_value._t)
		{
		case e_true:
			v = true;
			return true;

		case e_false:
			v = false;
			return true;

		default:break;
		}

		// fixme consider auto convert int to bool?
		return false;
	}

	bool Proxy::try_get(int& v) const
	{
		switch (_value._t)
		{
		case e_int:
			v = (int)_value._number;
			return true;

		case e_number:
			puts("warning truncating number into int"); // maybe settings for runtime-warnings?
			v = (int)_value._number;
			return true;

		default:break;
		}

		// fixme consider auto convert bool to int?
		return false;
	}

	bool Proxy::try_get(double& v) const
	{
		switch (_value._t)
		{
		case e_int:
		case e_number:
			v = _value._number;
			return true;

		default:break;
		}

		return false;
	}

	bool Proxy::try_get(float& v) const
	{
		switch (_value._t)
		{
		case e_int:
		case e_number:
			v = (float)_value._number;
			return true;

		default:break;
		}

		return false;
	}





	int Proxy::size() const
	{
		return _value._e - _value._b;
	}

	TextSpan Proxy::get_key(int i) const
	{
		const char* text = _parsed->_text._b;
		Key k = _parsed->_object_kvps[_value._b + i]._k;
		return { text + k._b, text + k._e };
	}

	Proxy Proxy::get_child(int i) const
	{
		switch (_value._t)
		{
		case e_array:
			if (i < size())
			{
				Value v = _parsed->_array_values[_value._b + i];
				return Proxy(v, _parsed);
			}
			break;

		case e_object:
			if (i < size())
			{
				Value v = _parsed->_object_kvps[_value._b + i]._v;
				return Proxy(v, _parsed);
			}
			break;

		default:break;
		}

		// only supported by array and object
		return Proxy({e_null, -1,-1, 0}, _parsed);
	}

	bool Proxy::keys_same(const HashedKey& a, Key b) const
	{
		// hash
		if (a._h != b._h)
			return false;

		// length
		int b_len = b._e - b._b;
		if (b_len != a._s)
			return false;

		// loop
		// if both hash and length match do we even care about the full loop here?
		int lim = b_len;
		const char* text_a = a._b;
		const char* text_b = _parsed->_text._b + b._b;
		for (int i = 0; i < lim; ++i)
		{
			if (text_a[i] != text_b[i])
				return false;
		}
		return true;
	}

	Proxy Proxy::get_child(HashedKey key) const
	{
		if (_value._t == e_object)
		{
			// compare all (hash-first)
			int lim = _value._e;
			for (int i = _value._b; i < lim; ++i)
			{
				const KvP& kvp = _parsed->_object_kvps[i];
				if (keys_same(key, kvp._k))
				{
					return Proxy(kvp._v, _parsed);
				}
			}
		}

		// if issue return empty proxy
		return Proxy({ e_null, -1,-1, 0 }, _parsed);
	}

	Proxy Proxy::get_child(HashedKeyStripped key) const
	{
		if (_value._t == e_object)
		{
			// compare all only hash+length
			int lim = _value._e;
			for (int i = _value._b; i < lim; ++i)
			{
				const KvP& kvp = _parsed->_object_kvps[i];
				const Key& k = kvp._k;
				if (key._h != k._h)
					continue;

				if (key._s != (k._e-k._b))
					continue;

				return Proxy(kvp._v, _parsed);
			}
		}

		// if issue return empty proxy
		return Proxy({ e_null, -1,-1, 0 }, _parsed);
	}





	bool Reader::parse(const char* text, int text_length, std::string* put_error_here)
	{
		// if verbose, stats do timings

		if (text_length < 0)
		{
			// calculate length if needed
			text_length = 0;
			while(text[text_length] != 0)
				++text_length;
		}

		Parser parser;
		parser.parse({text, text + text_length}, &_parsed);

		// check error
		if (!parser._error)
		{
			return true;
		}

		if (put_error_here != nullptr)
			*put_error_here = parser._error_description;
		else
			puts(parser._error_description.c_str());

		return false;
	}

	Proxy Reader::get_root()
	{
		return Proxy(_parsed._root, &_parsed);
	}










	// debug a parsed tree


	void put(TextSpan text)
	{
		for (; text._b < text._e; ++text._b)
		{
			putchar(*text._b);
		}
	}

	void print_tree_recursive(const Proxy& p, int indent)
	{
		// indent here
		switch (p.debug_get_type())
		{
		case e_object:
		{
			puts("{");
			int len = p.size();
			for (int i = 0; i < len; ++i)
			{
				put(p.get_key(i));
				putchar(':');
				print_tree_recursive(p.get_child(i), ++indent);
			}
			puts("}");
		}
		break;

		case e_array:
		{
			puts("[");
			int len = p.size();
			for (int i = 0; i < len; ++i)
			{
				print_tree_recursive(p.get_child(i), ++indent);
			}
			puts("]");
		}
		break;

		case e_int:
			int inum;
			p.try_get(inum);
			printf("[int] %d\n", inum);
			break;

		case e_number:
			double num;
			p.try_get(num);
			printf("[number] %f\n", num);
			break;

		case e_string:
		{
			TextSpan text = p.debug_get_as_raw_string();
			std::string unescaped = Proxy::unescape(text);
			printf("[string] %s\n", unescaped.c_str());
		}
		break;

		case e_true:			printf("[bool] true\n");			break;
		case e_false:			printf("[bool] false\n");			break;
		case e_null:			printf("[null] null\n");			break;

		default:
			printf("raw text: ");
			put(p.debug_get_as_raw_string());
			putchar('\n');
		}
	}

	void debug_print_tree(const Proxy& p)
	{
		print_tree_recursive(p, 0);
	}


}


