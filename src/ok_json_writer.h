#pragma once

#ifndef OK_JSON_WRITER_H
#define OK_JSON_WRITER_H

#include <cstdint>
#include <vector>
#include <string>

namespace OkJsonWriter
{
	enum Type
	{
		e_object,
		e_array,
	};

	struct Proxy;
	struct WriterHelper;

	// this is user-facing
	struct Writer
	{
		Writer();

		std::vector<char> _dest;

	private:
		struct Container
		{
			Proxy* _proxy; // to keep track of what proxy pushed this entry
			int _size = 0; // how many entries are added to this container

			Container(Proxy* p);
		};

		std::vector<Container> _stack;
		friend WriterHelper;
	};

	// the proxy is either an array or an object
	struct Proxy
	{
		// key == nullptr is adding only a value, only valid for array

		// Add container
		Proxy(Writer& writer, Type type, const char* key = nullptr);
		~Proxy();

		// Add scalar
		void add( const std::string&	value, const char* key = nullptr);
		void add( const char*			value, const char* key = nullptr);
		void add( double				value, const char* key = nullptr);
		void add( int					value, const char* key = nullptr);
		void add( bool					value, const char* key = nullptr);

		Writer& get_writer()
		{
			return _writer;
		};
	private:

		void add_common(const char* key);

		Type _type;
		Writer& _writer;

		friend WriterHelper;
	};
};

#endif // OK_JSON_WRITER_H
