#ifndef OK_JSON_READER_H
#define OK_JSON_READER_H

#include <cstdint>
#include <vector>
#include <string>

namespace OkJsonReader
{
	const uint64_t k_fnv1a_offset_basis = 0xcbf29ce484222325UL; // FNV-1a to speed up key-value access

	enum Type
	{
		e_object,
		e_array,
		e_int,		// 42
		e_number,	// 42.42e2
		e_string,
		e_true,
		e_false,
		e_null
	};

	struct Value
	{
		Type _t;
		int32_t _b;
		int32_t _e;
		double _number;
	};

	struct Key
	{
		int32_t _b; // string from text
		int32_t _e;
		uint64_t _h;
	};

	struct KvP
	{
		Key _k;
		Value _v;
	};

	struct TextSpan
	{
		const char* _b;
		const char* _e;

		TextSpan()
		{
			_b = nullptr;
			_e = nullptr;
		}

		TextSpan(const char* b, const char* e)
		{
			_b = b;
			_e = e;
		}

		TextSpan(const char* v)
		{
			_b = v;
			while (*v != 0)
				++v;

			_e = v;
		}
	};

	struct Parsed
	{
		TextSpan _text; // strings index into source-text
		
		// arrays
		std::vector<Value> _array_values; // arrays index into here

		// objects
		std::vector<KvP> _object_kvps; // objects index into here
		
		Value _root { e_null, -1, -1, 0 }; // null-value
	};

	///////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////

	// Maybe allow these to be created fron text-spans too (to embed all in one string)
	struct HashedKey
	{
		const char* _b = nullptr;
		uint64_t _h = k_fnv1a_offset_basis; // hash
		int32_t _s = 0;

		// key has to include escape codes (to match json-file)
		HashedKey(const char* text);
	};

	// same as above but forget about full text
	struct HashedKeyStripped
	{
		uint64_t _h; // hash
		int32_t _s; // size

		// key to include escape codes (to match json-file)
		static HashedKeyStripped from_string(const char* text);
	};

	struct Reader;

	// "proxy" objects, used on "parsed" data
	struct Proxy
	{
		Type debug_get_type() const;
		TextSpan debug_get_as_raw_string() const; // available for all types (not objects or arrays) "raw" means that escape codes are still in here

		static std::string unescape(TextSpan text); // applies escape-codes

		// simplest getters (returns valid)
		bool try_get(TextSpan& v) const;
		bool try_get(std::string& v) const;
		bool try_get(bool& v) const;
		bool try_get(int& v) const;
		bool try_get(double& v) const;
		bool try_get(float& v) const;

		int size() const; // valid for string, array and object only
		TextSpan get_key(int i) const; // valid for object only
		Proxy get_child(int i) const; // valid for array and object only
		Proxy get_child(HashedKey key_string) const; // valid only for object, get value by key
		Proxy get_child(HashedKeyStripped key) const; // same as above but skips full string-compare (only hash + length)

	private:
		Proxy(Value value, const Parsed* parsed);
		bool keys_same(const HashedKey& a, Key b) const;

		Value _value;
		const Parsed* _parsed;

		friend Reader;
	};

	///////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////

	// parser
	struct Reader
	{
		bool parse(const char* text, int text_length = -1, std::string* put_error_here = nullptr);

		// warning, the proxy-objects will point to the submitted text above
		Proxy get_root();

	private:
		Parsed _parsed;
	};

	void debug_print_tree(const Proxy& p);
};

#endif // OK_JSON_READER_H
