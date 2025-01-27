#include "ok_json_writer.h"

#include <vector>


namespace OkJsonWriter
{
	//////////////////////////////////////////////
	struct WriterHelper
	{
		static void add_comma_if_needed(Writer& w, Proxy* p)
		{
			if (w._stack.empty())
			{
				// never needed for root
				return;
			}

			auto& b = w._stack.back();

			// if p == null, we are pushing to back
			if (p != nullptr)
			{
				if (b._proxy != p)
				{
					// not on top!
					// error
					return;
				}
			}

			if (b._size != 0)
			{
				w._dest.push_back(','); // comma needed
			}

			// add here
			++b._size;
		}

		static void add_string(Writer& w, const char* str)
		{
			for (;;)
			{
				int v = *str;
				if (v == 0)
				{
					break;
				}

				w._dest.push_back(v);
				++str;
			}
		}

		static void add_number(Writer& w, double v)
		{
			const int k_bufs = 64;
			char buf[k_bufs];
			snprintf(buf, k_bufs, "%f", v);
			// remove trailing 0s?
			add_string(w, buf);
		}

		static void add_int(Writer& w, int v)
		{
			const int k_bufs = 64;
			char buf[k_bufs];
			snprintf(buf, k_bufs, "%d", v);
			add_string(w, buf);
		}

		static void add_key(Writer& w, const char* key)
		{
			w._dest.push_back('\"'); // add quotes to key
			add_string(w, key);
			w._dest.push_back('\"'); // add quotes to key
			w._dest.push_back(':');

		}

		static void push_to_stack(Writer& w, Proxy* p)
		{
			w._dest.push_back( p->_type == e_object ? '{' : '[' );

			Writer::Container c(p);
			w._stack.push_back(c);
		}

		static void pop_stack(Writer& w, Proxy* p)
		{
			if (w._stack.empty())
			{
				// never needed for root
				return;
			}

			auto& b = w._stack.back();
			if (b._proxy != p)
			{
				// not on top!
				// error
				return;
			}

			w._dest.push_back(p->_type == e_object ? '}' : ']');
			w._stack.pop_back();
		}


	};


	//////////////////////////////////////////////
	Writer::Writer()
	{
		// write a comment
		WriterHelper::add_string(*this, "// ok_json 0.2\n");
	}

	//////////////////////////////////////////////
	Writer::Container::Container(Proxy* p)
		:_proxy(p)
		,_size(0)
	{
	}

	//////////////////////////////////////////////
	Proxy::Proxy(Writer& writer, Type type, const char* key)
		:_type(type)
		,_writer(writer)
	{
		WriterHelper::add_comma_if_needed(writer, nullptr);

		if (key != nullptr)
		{
			WriterHelper::add_key(writer, key);
		}

		WriterHelper::push_to_stack(writer, this);
	}

	Proxy::~Proxy()
	{
		WriterHelper::pop_stack(_writer, this);
	}

	// fixme kvp common

	void Proxy::add_common(const char* key)
	{
		WriterHelper::add_comma_if_needed(_writer, this);

		// ensure type is object
		if (key != nullptr)
		{
			if (_type != e_object)
			{
				puts("Proxy::add_kvp _type != e_object");
				return;
			}
			WriterHelper::add_key(_writer, key);
		}
		else
		{
			if (_type != e_array)
			{
				puts("Proxy::add_kvp _type != e_array");
				return;
			}
		}

	}

	void Proxy::add(const std::string& value, const char* key)
	{
		add_common(key);
		_writer._dest.push_back('\"');
		WriterHelper::add_string(_writer, value.c_str());
		_writer._dest.push_back('\"');
	}

	void Proxy::add(const char* value, const char* key)
	{
		add_common(key);
		_writer._dest.push_back('\"');
		WriterHelper::add_string(_writer, value);
		_writer._dest.push_back('\"');
	}

	void Proxy::add(double value, const char* key)
	{
		add_common(key);
		WriterHelper::add_number(_writer, value);
	}

	void Proxy::add(int value, const char* key)
	{
		add_common(key);
		WriterHelper::add_int(_writer, value);
	}

	void Proxy::add(bool value, const char* key)
	{
		add_common(key);
		WriterHelper::add_string(_writer, value ? "true" : "false");
	}


};


