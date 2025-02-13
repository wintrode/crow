#pragma once

#include <string>
#include <unordered_map>
#include <boost/algorithm/string.hpp>
#include <algorithm>

#include "crow/http_parser_merged.h"
#include "crow/http_request.h"

typedef void (*data_fn_t)(http_parser *self_, const char* at, size_t length);
typedef void (*data_done_fn_t)(http_parser *self_);

namespace crow
{

  template <typename Handler>
    struct HTTPParser : public http_parser
    {
        static int on_message_begin(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->clear();
            return 0;
        }
        static int on_url(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            self->raw_url.insert(self->raw_url.end(), at, at+length);
            return 0;
        }
        static int on_header_field(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            switch (self->header_building_state)
            {
                case 0:
                    if (!self->header_value.empty())
                    {
                        self->headers.emplace(std::move(self->header_field), std::move(self->header_value));
                    }
                    self->header_field.assign(at, at+length);
                    self->header_building_state = 1;
                    break;
                case 1:
                    self->header_field.insert(self->header_field.end(), at, at+length);
                    break;
            }
            return 0;
        }
        static int on_header_value(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            switch (self->header_building_state)
            {
                case 0:
                    self->header_value.insert(self->header_value.end(), at, at+length);
                    break;
                case 1:
                    self->header_building_state = 0;
                    self->header_value.assign(at, at+length);
                    break;
            }
            return 0;
        }
        static int on_headers_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
            if (!self->header_field.empty())
            {
	      self->headers.emplace(std::move(self->header_field), std::move(self->header_value));

            }
            self->process_header();

            return 0;
        }
        static int on_body(http_parser* self_, const char* at, size_t length)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);
	    // adding to body string
	    if (self->data_callback) 
	      self->data_callback(self_, at, length);
	    else 
	      self->body.insert(self->body.end(), at, at+length);
            return 0;
        }
        static int on_message_complete(http_parser* self_)
        {
            HTTPParser* self = static_cast<HTTPParser*>(self_);

	    if (self->data_done_callback) 
	      self->data_done_callback(self_);

            // url params
            self->url = self->raw_url.substr(0, self->raw_url.find("?"));
            self->url_params = query_string(self->raw_url);

            self->process_message();
            return 0;
        }
        HTTPParser(Handler* handler) :
            handler_(handler)
        {
            http_parser_init(this, HTTP_REQUEST);
        }

        // return false on error
        bool feed(const char* buffer, int length)
        {
            const static http_parser_settings settings_{
                on_message_begin,
                on_url,
                nullptr,
                on_header_field,
                on_header_value,
                on_headers_complete,
                on_body,
                on_message_complete,
            };

            int nparsed = http_parser_execute(this, &settings_, buffer, length);
            return nparsed == length;
        }

        bool done()
        {
            return feed(nullptr, 0);
        }

        void clear()
        {
            url.clear();
            raw_url.clear();
            header_building_state = 0;
            header_field.clear();
            header_value.clear();
            headers.clear();
            url_params.clear();
            body.clear();
        }

        void process_header()
        {
            handler_->handle_header();
        }

        void process_message()
        {
            handler_->handle();
        }

        request to_request() const
        {
            return request{(HTTPMethod)method, std::move(raw_url), std::move(url), std::move(url_params), std::move(headers), std::move(body)};
        }
      
        bool is_upgrade() const
        { 
	    return upgrade;
        }
      
        bool check_version(int major, int minor) const
        {
            return http_major == major && http_minor == minor;
        }

        std::string raw_url;
        std::string url;

        int header_building_state = 0;
        std::string header_field;
        std::string header_value;
        ci_map headers;
        query_string url_params;
        std::string body;

        Handler* handler_;

        // allow for custom post data handler
        //void (*data_callback)(http_parser *self_, const char* at, size_t length) = 0;
        data_fn_t data_callback;
        data_done_fn_t data_done_callback;
        //void (*data_done_callback)(http_parser *self_) = 0;
    };
}
