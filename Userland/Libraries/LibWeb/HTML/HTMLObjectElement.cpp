/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Bitmap.h>
#include <LibWeb/CSS/StyleComputer.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/HTML/HTMLObjectElement.h>
#include <LibWeb/Layout/ImageBox.h>
#include <LibWeb/Loader/ResourceLoader.h>

namespace Web::HTML {

HTMLObjectElement::HTMLObjectElement(DOM::Document& document, DOM::QualifiedName qualified_name)
    : FormAssociatedElement(document, move(qualified_name))
{
}

HTMLObjectElement::~HTMLObjectElement() = default;

void HTMLObjectElement::parse_attribute(const FlyString& name, const String& value)
{
    HTMLElement::parse_attribute(name, value);

    if (name == HTML::AttributeNames::data)
        queue_element_task_to_run_object_representation_steps();
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#attr-object-data
String HTMLObjectElement::data() const
{
    auto data = attribute(HTML::AttributeNames::data);
    return document().parse_url(data).to_string();
}

RefPtr<Layout::Node> HTMLObjectElement::create_layout_node(NonnullRefPtr<CSS::StyleProperties> style)
{
    if (m_should_show_fallback_content)
        return HTMLElement::create_layout_node(move(style));
    if (m_image_loader.has_value() && m_image_loader->has_image())
        return adopt_ref(*new Layout::ImageBox(document(), *this, move(style), *m_image_loader));
    return nullptr;
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element:queue-an-element-task
void HTMLObjectElement::queue_element_task_to_run_object_representation_steps()
{
    queue_an_element_task(HTML::Task::Source::DOMManipulation, [&]() {
        // 1. FIXME: If the user has indicated a preference that this object element's fallback content be shown instead of the element's usual behavior, then jump to the step below labeled fallback.
        // 2. FIXME: If the element has an ancestor media element, or has an ancestor object element that is not showing its fallback content, or if the element is not in a document whose browsing context is non-null, or if the element's node document is not fully active, or if the element is still in the stack of open elements of an HTML parser or XML parser, or if the element is not being rendered, then jump to the step below labeled fallback.
        // 3. FIXME: If the classid attribute is present, and has a value that isn't the empty string, then: if the user agent can find a plugin suitable according to the value of the classid attribute, and plugins aren't being sandboxed, then that plugin should be used, and the value of the data attribute, if any, should be passed to the plugin. If no suitable plugin can be found, or if the plugin reports an error, jump to the step below labeled fallback.

        // 4. If the data attribute is present and its value is not the empty string, then:
        if (auto data = attribute(HTML::AttributeNames::data); !data.is_empty()) {
            // 1. If the type attribute is present and its value is not a type that the user agent supports, and is not a type that the user agent can find a plugin for, then the user agent may jump to the step below labeled fallback without fetching the content to examine its real type.

            // 2. Parse a URL given the data attribute, relative to the element's node document.
            auto url = document().parse_url(data);

            // 3. If that failed, fire an event named error at the element, then jump to the step below labeled fallback.
            if (!url.is_valid()) {
                dispatch_event(DOM::Event::create(HTML::EventNames::error));
                return run_object_representation_fallback_steps();
            }

            // 4. Let request be a new request whose URL is the resulting URL record, client is the element's node document's relevant settings object, destination is "object", credentials mode is "include", mode is "navigate", and whose use-URL-credentials flag is set.
            auto request = LoadRequest::create_for_url_on_page(url, document().page());

            // 5. Fetch request, with processResponseEndOfBody given response res set to finalize and report timing with res, the element's node document's relevant global object, and "object".
            //    Fetching the resource must delay the load event of the element's node document until the task that is queued by the networking task source once the resource has been fetched (defined next) has been run.
            set_resource(ResourceLoader::the().load_resource(Resource::Type::Generic, request));

            // 6. If the resource is not yet available (e.g. because the resource was not available in the cache, so that loading the resource required making a request over the network), then jump to the step below labeled fallback. The task that is queued by the networking task source once the resource is available must restart this algorithm from this step. Resources can load incrementally; user agents may opt to consider a resource "available" whenever enough data has been obtained to begin processing the resource.
            // NOTE: The request is always asynchronous, even if the success callback is immediately queued for execution.
        }

        // 5. If the data attribute is absent but the type attribute is present, and the user agent can find a plugin suitable according to the value of the type attribute, and plugins aren't being sandboxed, then that plugin should be used. If these conditions cannot be met, or if the plugin reports an error, jump to the step below labeled fallback. Otherwise return; once the plugin is completely loaded, queue an element task on the DOM manipulation task source given the object element to fire an event named load at the element.
        run_object_representation_fallback_steps();
    });
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element:concept-event-fire-2
void HTMLObjectElement::resource_did_fail()
{
    // 4.7. If the load failed (e.g. there was an HTTP 404 error, there was a DNS error), fire an event named error at the element, then jump to the step below labeled fallback.
    dispatch_event(DOM::Event::create(HTML::EventNames::error));
    run_object_representation_fallback_steps();
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#object-type-detection
void HTMLObjectElement::resource_did_load()
{
    // 4.8. Determine the resource type, as follows:

    // 1. Let the resource type be unknown.
    String resource_type = "unknown"sv;

    // 2. FIXME: If the user agent is configured to strictly obey Content-Type headers for this resource, and the resource has associated Content-Type metadata, then let the resource type be the type specified in the resource's Content-Type metadata, and jump to the step below labeled handler.
    // 3. FIXME: If there is a type attribute present on the object element, and that attribute's value is not a type that the user agent supports, but it is a type that a plugin supports, then let the resource type be the type specified in that type attribute, and jump to the step below labeled handler.

    // 4. Run the appropriate set of steps from the following list:
    // * If the resource has associated Content-Type metadata
    if (auto it = resource()->response_headers().find("Content-Type"sv); it != resource()->response_headers().end()) {
        // 1. Let binary be false.
        bool binary = false;

        // 2. FIXME: If the type specified in the resource's Content-Type metadata is "text/plain", and the result of applying the rules for distinguishing if a resource is text or binary to the resource is that the resource is not text/plain, then set binary to true.

        // 3. If the type specified in the resource's Content-Type metadata is "application/octet-stream", then set binary to true.
        if (it->value == "application/octet-stream"sv)
            binary = true;

        // 4. If binary is false, then let the resource type be the type specified in the resource's Content-Type metadata, and jump to the step below labeled handler.
        if (!binary)
            return run_object_representation_handler_steps(it->value);

        // 5. If there is a type attribute present on the object element, and its value is not application/octet-stream, then run the following steps:
        if (auto type = this->type(); !type.is_empty() && (type != "application/octet-stream"sv)) {
            // 1. If the attribute's value is a type that a plugin supports, or the attribute's value is a type that starts with "image/" that is not also an XML MIME type, then let the resource type be the type specified in that type attribute.
            // FIXME: This only partially implements this step.
            if (type.starts_with("image/"sv))
                resource_type = type;

            // 2. Jump to the step below labeled handler.
        }
    }
    // * Otherwise, if the resource does not have associated Content-Type metadata
    else {
        String tentative_type;

        // 1. If there is a type attribute present on the object element, then let the tentative type be the type specified in that type attribute.
        //    Otherwise, let tentative type be the computed type of the resource.
        if (auto type = this->type(); !type.is_empty())
            tentative_type = move(type);
        else
            tentative_type = resource()->mime_type();

        // 2. If tentative type is not application/octet-stream, then let resource type be tentative type and jump to the step below labeled handler.
        if (tentative_type != "application/octet-stream"sv)
            resource_type = move(tentative_type);
    }

    // 5. FIXME: If applying the URL parser algorithm to the URL of the specified resource (after any redirects) results in a URL record whose path component matches a pattern that a plugin supports, then let resource type be the type that that plugin can handle.

    run_object_representation_handler_steps(resource_type);
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element:plugin-11
void HTMLObjectElement::run_object_representation_handler_steps(StringView resource_type)
{
    // 4.9. Handler: Handle the content as given by the first of the following cases that matches:

    // * FIXME: If the resource type is not a type that the user agent supports, but it is a type that a plugin supports
    //     If the object element's nested browsing context is non-null, then it must be discarded and then set to null.
    //     If plugins are being sandboxed, then jump to the step below labeled fallback.
    //     Otherwise, the user agent should use the plugin that supports resource type and pass the content of the resource to that plugin. If the plugin reports an error, then jump to the step below labeled fallback.

    // * FIXME: If the resource type is an XML MIME type, or if the resource type does not start with "image/"
    //     If the object element's nested browsing context is null, then create a new nested browsing context for the element.
    //     If the URL of the given resource does not match about:blank, then navigate the element's nested browsing context to that resource, with historyHandling set to "replace" and the source browsing context set to the object element's node document's browsing context. (The data attribute of the object element doesn't get updated if the browsing context gets further navigated to other locations.)
    //     The object element represents its nested browsing context.

    // * If the resource type starts with "image/", and support for images has not been disabled
    if (resource_type.starts_with("image/"sv)) {
        // FIXME: If the object element's nested browsing context is non-null, then it must be discarded and then set to null.

        // Apply the image sniffing rules to determine the type of the image.
        // The object element represents the specified image.
        // If the image cannot be rendered, e.g. because it is malformed or in an unsupported format, jump to the step below labeled fallback.
        if (!resource()->has_encoded_data())
            return run_object_representation_fallback_steps();

        convert_resource_to_image();
    }

    // * Otherwise
    else {
        // The given resource type is not supported. Jump to the step below labeled fallback.
        run_object_representation_fallback_steps();
    }
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element:the-object-element-19
void HTMLObjectElement::run_object_representation_completed_steps()
{
    // 4.10. The element's contents are not part of what the object element represents.
    // 4.11. If the object element does not represent its nested browsing context, then once the resource is completely loaded, queue an element task on the DOM manipulation task source given the object element to fire an event named load at the element.
    queue_an_element_task(HTML::Task::Source::DOMManipulation, [&]() {
        dispatch_event(DOM::Event::create(HTML::EventNames::load));
    });

    m_should_show_fallback_content = false;

    set_needs_style_update(true);
    document().set_needs_layout();

    // 4.12. Return.
}

void HTMLObjectElement::run_object_representation_fallback_steps()
{
    // 6. Fallback: The object element represents the element's children, ignoring any leading param element children. This is the element's fallback content. If the element has an instantiated plugin, then unload it. If the element's nested browsing context is non-null, then it must be discarded and then set to null.
    m_should_show_fallback_content = true;

    set_needs_style_update(true);
    document().set_needs_layout();
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element:the-object-element-23
void HTMLObjectElement::convert_resource_to_image()
{
    // FIXME: This is a bit awkward. We convert the Resource to an ImageResource here because we do not know
    //        until now that the resource is an image. ImageLoader then becomes responsible for handling
    //        encoding failures, animations, etc. It would be clearer if those features were split from
    //        ImageLoader into a purpose build class to be shared between here and ImageBox.
    m_image_loader.emplace(*this);

    m_image_loader->on_load = [this] {
        run_object_representation_completed_steps();
    };
    m_image_loader->on_fail = [this] {
        run_object_representation_fallback_steps();
    };

    m_image_loader->adopt_object_resource({}, *resource());
    set_resource(nullptr);
}

}
