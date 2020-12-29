#ifdef BLAH_USE_D3D11

#include <blah/internal/graphics_backend.h>
#include <blah/internal/platform_backend.h>
#include <blah/log.h>
#include <blah/app.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

namespace Blah
{
	class D3D11_Shader;

	struct D3D11
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGISwapChain* swap_chain = nullptr;
		ID3D11RenderTargetView* backbuffer = nullptr;
		ID3D11RasterizerState* rasterizer = nullptr;
		ID3D11SamplerState* sampler = nullptr;
		ID3D11DepthStencilState* depth_state = nullptr;
		RendererFeatures features;
		Point last_size;

		struct InputLayoutEntry
		{
			uint32_t shader_hash;
			VertexFormat format;
			ID3D11InputLayout* layout;
		};

		struct BlendStateEntry
		{
			BlendMode blend;
			ID3D11BlendState* state;
		};

		Vector<InputLayoutEntry> layout_cache;
		Vector<BlendStateEntry> blend_cache;

		ID3D11InputLayout* get_layout(D3D11_Shader* shader, const VertexFormat& format);
		ID3D11BlendState* get_blend(const BlendMode& blend);
	};

	D3D11 state;

	D3D11_BLEND_OP blend_op(BlendOp op)
	{
		switch (op)
		{
		case BlendOp::Add: return D3D11_BLEND_OP_ADD;
		case BlendOp::Subtract: return D3D11_BLEND_OP_SUBTRACT;
		case BlendOp::ReverseSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
		case BlendOp::Min: return D3D11_BLEND_OP_MIN;
		case BlendOp::Max: return D3D11_BLEND_OP_MAX;
		}

		return D3D11_BLEND_OP_ADD;
	}

	D3D11_BLEND blend_factor(BlendFactor factor)
	{
		switch (factor)
		{
		case BlendFactor::Zero: return D3D11_BLEND_ZERO;
		case BlendFactor::One: return D3D11_BLEND_ONE;
		case BlendFactor::SrcColor: return D3D11_BLEND_SRC_COLOR;
		case BlendFactor::OneMinusSrcColor: return D3D11_BLEND_INV_SRC_COLOR;
		case BlendFactor::DstColor: return D3D11_BLEND_DEST_COLOR;
		case BlendFactor::OneMinusDstColor: return D3D11_BLEND_INV_DEST_COLOR;
		case BlendFactor::SrcAlpha: return D3D11_BLEND_SRC_ALPHA;
		case BlendFactor::OneMinusSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
		case BlendFactor::DstAlpha: return D3D11_BLEND_DEST_ALPHA;
		case BlendFactor::OneMinusDstAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
		case BlendFactor::ConstantColor: return D3D11_BLEND_BLEND_FACTOR;
		case BlendFactor::OneMinusConstantColor: return D3D11_BLEND_INV_BLEND_FACTOR;
		case BlendFactor::ConstantAlpha: return D3D11_BLEND_BLEND_FACTOR;
		case BlendFactor::OneMinusConstantAlpha: return D3D11_BLEND_INV_BLEND_FACTOR;
		case BlendFactor::SrcAlphaSaturate: return D3D11_BLEND_SRC_ALPHA_SAT;
		case BlendFactor::Src1Color: return D3D11_BLEND_SRC1_COLOR;
		case BlendFactor::OneMinusSrc1Color: return D3D11_BLEND_INV_SRC1_COLOR;
		case BlendFactor::Src1Alpha: return D3D11_BLEND_SRC1_ALPHA;
		case BlendFactor::OneMinusSrc1Alpha: return D3D11_BLEND_INV_SRC1_ALPHA;
		}

		return D3D11_BLEND_ZERO;
	}

	bool reflect_uniforms(Vector<UniformInfo>& append_to, Vector<ID3D11Buffer*>& buffers, ID3DBlob* shader, ShaderType shader_type)
	{
		ID3D11ShaderReflection* reflector = nullptr;
		D3DReflect(shader->GetBufferPointer(), shader->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflector);

		D3D11_SHADER_DESC shader_desc;
		reflector->GetDesc(&shader_desc);
		
		for (int i = 0; i < shader_desc.BoundResources; i++)
		{
			D3D11_SHADER_INPUT_BIND_DESC desc;
			reflector->GetResourceBindingDesc(i, &desc);

			if (desc.Type == D3D_SIT_TEXTURE && 
				desc.Dimension == D3D_SRV_DIMENSION_TEXTURE2D)
			{
				auto uniform = append_to.expand();
				uniform->name = desc.Name;
				uniform->shader = shader_type;
				uniform->buffer_index = 0;
				uniform->array_length = max(1, desc.BindCount);
				uniform->type = UniformType::Texture;
			}
		}

		for (int i = 0; i < shader_desc.ConstantBuffers; i++)
		{
			D3D11_SHADER_BUFFER_DESC desc;

			auto cb = reflector->GetConstantBufferByIndex(i);
			cb->GetDesc(&desc);

			// create the constant buffer for assigning data later
			{
				D3D11_BUFFER_DESC buffer_desc = {};
				buffer_desc.ByteWidth = desc.Size;
				buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
				buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				ID3D11Buffer* buffer;
				state.device->CreateBuffer(&buffer_desc, nullptr, &buffer);
				buffers.push_back(buffer);
			}

			// get the uniforms
			for (int j = 0; j < desc.Variables; j++)
			{
				D3D11_SHADER_VARIABLE_DESC var_desc;
				D3D11_SHADER_TYPE_DESC type_desc;

				auto var = cb->GetVariableByIndex(j);
				var->GetDesc(&var_desc);

				auto type = var->GetType();
				type->GetDesc(&type_desc);

				auto uniform = append_to.expand();
				uniform->name = var_desc.Name;
				uniform->shader = shader_type;
				uniform->buffer_index = i;
				uniform->array_length = max(1, type_desc.Elements);
				uniform->type = UniformType::None;

				if (type_desc.Type == D3D_SVT_FLOAT)
				{
					if (type_desc.Rows == 1)
					{
						if (type_desc.Columns == 1)
							uniform->type = UniformType::Float;
						else if (type_desc.Columns == 2)
							uniform->type = UniformType::Float2;
						else if (type_desc.Columns == 3)
							uniform->type = UniformType::Float3;
						else if (type_desc.Columns == 4)
							uniform->type = UniformType::Float4;
					}
					else if (type_desc.Rows == 2 && type_desc.Columns == 3)
					{
						uniform->type = UniformType::Mat3x2;
					}
					else if (type_desc.Rows == 4 && type_desc.Columns == 4)
					{
						uniform->type = UniformType::Mat4x4;
					}
				}
			}
		}

		return true;
	}

	class D3D11_Texture : public Texture
	{
	private:
		int m_width;
		int m_height;
		TextureFilter m_filter;
		TextureFormat m_format;
		TextureWrap m_wrap_x;
		TextureWrap m_wrap_y;
		bool m_is_framebuffer;
		int m_size;

	public:
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* view = nullptr;

		D3D11_Texture(int width, int height, TextureFilter filter, TextureWrap wrap_x, TextureWrap wrap_y, TextureFormat format, bool is_framebuffer)
		{
			m_width = width;
			m_height = height;
			m_filter = filter;
			m_format = format;
			m_wrap_x = wrap_x;
			m_wrap_y = wrap_y;
			m_is_framebuffer = is_framebuffer;
			m_size = 0;

			D3D11_TEXTURE2D_DESC desc = { 0 };
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;

			if (is_framebuffer)
				desc.BindFlags |= D3D11_BIND_RENDER_TARGET;

			switch (format)
			{
			case TextureFormat::R:
				desc.Format = DXGI_FORMAT_R8_UNORM;
				m_size = width * height;
				break;
			case TextureFormat::RG:
				desc.Format = DXGI_FORMAT_R8G8_UNORM;
				m_size = width * height * 2;
				break;
			case TextureFormat::RGBA:
				desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				m_size = width * height * 4;
				break;
			case TextureFormat::DepthStencil:
				desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				m_size = width * height * 4;
				break;
			}

			auto hr = state.device->CreateTexture2D(&desc, NULL, &texture);
			if (!SUCCEEDED(hr))
			{
				if (texture)
					texture->Release();
				texture = nullptr;
				return;
			}

			hr = state.device->CreateShaderResourceView(texture, NULL, &view);
			if (!SUCCEEDED(hr))
			{
				texture->Release();
				texture = nullptr;
			}
		}

		~D3D11_Texture()
		{
			if (texture)
				texture->Release();
			texture = nullptr;
		}

		virtual int width() const override
		{
			return m_width;
		}

		virtual int height() const override
		{
			return m_height;
		}

		virtual TextureFormat format() const override
		{
			return m_format;
		}

		virtual void set_filter(TextureFilter filter) override
		{
			m_filter = filter;
		}

		virtual TextureFilter get_filter() const override
		{
			return m_filter;
		}

		virtual void set_wrap(TextureWrap x, TextureWrap y) override
		{
			m_wrap_x = x;
			m_wrap_y = y;
		}

		virtual TextureWrap get_wrap_x() const override
		{
			return m_wrap_x;
		}

		virtual TextureWrap get_wrap_y() const override
		{
			return m_wrap_y;
		}

		virtual void set_data(unsigned char* data) override
		{
			D3D11_BOX box;
			box.left = 0;
			box.right = m_width;
			box.top = 0;
			box.bottom = m_height;
			box.front = 0;
			box.back = 1;

			state.context->UpdateSubresource(
				texture,
				0,
				&box,
				data,
				m_size / m_height,
				0);
		}

		virtual void get_data(unsigned char* data) override
		{

		}

		virtual bool is_framebuffer() const override
		{
			return m_is_framebuffer;
		}

	};

	class D3D11_FrameBuffer : public FrameBuffer
	{
	private:
		Attachments m_attachments;

	public:
		StackVector<ID3D11RenderTargetView*, Attachments::MaxCapacity - 1> color_views;
		ID3D11DepthStencilView* depth_view = nullptr;

		D3D11_FrameBuffer(int width, int height, const TextureFormat* attachments, int attachmentCount)
		{
			for (int i = 0; i < attachmentCount; i++)
			{
				auto tex = new D3D11_Texture(
					width, height,
					TextureFilter::Linear,
					TextureWrap::Repeat,
					TextureWrap::Repeat,
					attachments[i],
					true);
				m_attachments.push_back(TextureRef(tex));

				if (attachments[i] == TextureFormat::DepthStencil)
				{
					state.device->CreateDepthStencilView(tex->texture, nullptr, &depth_view);
				}
				else
				{
					ID3D11RenderTargetView* view = nullptr;
					state.device->CreateRenderTargetView(tex->texture, nullptr, &view);
					color_views.push_back(view);
				}
			}
		}

		~D3D11_FrameBuffer()
		{
			for (auto& it : color_views)
				it->Release();
			color_views.clear();
		}

		virtual Attachments& attachments() override
		{
			return m_attachments;
		}

		virtual const Attachments& attachments() const override
		{
			return m_attachments;
		}

		virtual TextureRef& attachment(int index) override
		{
			return m_attachments[index];
		}

		virtual const TextureRef& attachment(int index) const override
		{
			return m_attachments[index];
		}

		virtual int width() const override
		{
			return m_attachments[0]->width();
		}

		virtual int height() const override
		{
			return m_attachments[0]->height();
		}

		virtual void clear(Color color) override
		{
			float col[4] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };

			for (int i = 0; i < m_attachments.size(); i++)
				state.context->ClearRenderTargetView(color_views[i], col);
		}
	};

	class D3D11_Shader : public Shader
	{
	public:
		ID3D11VertexShader* vertex = nullptr;
		ID3D11PixelShader* fragment = nullptr;
		ID3DBlob* vertex_blob = nullptr;
		ID3DBlob* fragment_blob = nullptr;
		Vector<ID3D11Buffer*> vcb;
		Vector<ID3D11Buffer*> fcb;
		StackVector<ShaderData::HLSL_Attribute, 16> attributes;
		Vector<UniformInfo> uniform_list;
		uint32_t hash = 0;

		D3D11_Shader(const ShaderData* data)
		{
			UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG;
			ID3DBlob* error_blob = nullptr;
			HRESULT hr;

			// compile vertex shader
			{
				hr = D3DCompile(
					data->vertex,
					strlen(data->vertex),
					nullptr,
					nullptr,
					nullptr,
					"vs_main",
					"vs_5_0",
					flags,
					0,
					&vertex_blob,
					&error_blob);

				if (FAILED(hr))
				{
					if (error_blob)
					{
						Log::error("%s", (char*)error_blob->GetBufferPointer());
						error_blob->Release();
					}

					return;
				}
			}

			// compile fragment shader
			{
				hr = D3DCompile(
					data->fragment,
					strlen(data->fragment),
					nullptr,
					nullptr,
					nullptr,
					"ps_main",
					"ps_5_0",
					flags,
					0,
					&fragment_blob,
					&error_blob);

				if (FAILED(hr))
				{
					if (error_blob)
					{
						Log::error("%s", (char*)error_blob->GetBufferPointer());
						error_blob->Release();
					}

					return;
				}
			}

			// create vertex shader
			{
				hr = state.device->CreateVertexShader(
					vertex_blob->GetBufferPointer(),
					vertex_blob->GetBufferSize(),
					NULL,
					&vertex);

				if (!SUCCEEDED(hr))
					return;
			}

			// create fragment shader
			{
				hr = state.device->CreatePixelShader(
					fragment_blob->GetBufferPointer(),
					fragment_blob->GetBufferSize(),
					NULL,
					&fragment);

				if (!SUCCEEDED(hr))
					return;
			}

			// get uniforms
			reflect_uniforms(uniform_list, vcb, vertex_blob, ShaderType::Vertex);
			reflect_uniforms(uniform_list, fcb, fragment_blob, ShaderType::Fragment);

			// combine uniforms that were in both
			for (int i = 0; i < uniform_list.size(); i++)
			{
				for (int j = i + 1; j < uniform_list.size(); j++)
				{
					if (strcmp(uniform_list[i].name, uniform_list[j].name) == 0)
					{
						if (uniform_list[i].type == uniform_list[j].type)
						{
							uniform_list[i].shader = (ShaderType)((int)uniform_list[i].shader | (int)uniform_list[j].shader);
							uniform_list.erase(j);
							j--;
						}
						else
						{
							// TODO:
							// We don't allow uniforms to share names ...
							// This should result in an invalid shader
						}
					}
				}
			}

			// TODO:
			// Validate Uniforms! Make sure they're all types we understand
			// (ex. float/matrix only)

			// copy HLSL attributes
			attributes = data->hlsl_attributes;

			// store hash
			hash = 5381;
			for (auto& it : attributes)
			{
				for (int i = 0, n = strlen(it.semantic_name); i < n; i++)
					hash = ((hash << 5) + hash) + it.semantic_name[i];
				hash = it.semantic_index << 5 + hash;
			}
		}

		~D3D11_Shader()
		{
			if (vertex) vertex->Release();
			if (vertex_blob) vertex_blob->Release();
			if (fragment) fragment->Release();
			if (fragment_blob) fragment_blob->Release();

			vertex = nullptr;
			vertex_blob = nullptr;
			fragment = nullptr;
			fragment_blob = nullptr;
		}

		virtual Vector<UniformInfo>& uniforms() override
		{
			return uniform_list;
		}

		virtual const Vector<UniformInfo>& uniforms() const override
		{
			return uniform_list;
		}
	};

	class D3D11_Mesh : public Mesh
	{
	private:
		int64_t m_vertex_count = 0;
		int64_t m_vertex_capacity = 0;
		int64_t m_index_count = 0;
		int64_t m_index_capacity = 0;

	public:
		ID3D11Buffer* vertex_buffer = nullptr;
		VertexFormat vertex_format;
		ID3D11Buffer* index_buffer = nullptr;
		IndexFormat index_format = IndexFormat::UInt16;
		int index_stride = 0;

		D3D11_Mesh()
		{

		}

		~D3D11_Mesh()
		{
			if (vertex_buffer)
				vertex_buffer->Release();
			if (index_buffer)
				index_buffer->Release();
		}

		virtual void index_data(IndexFormat format, const void* indices, int64_t count) override
		{
			m_index_count = count;

			if (index_format != format || !index_buffer || m_index_count > m_index_capacity)
			{
				index_stride = 0;
				index_format = format;
				m_index_capacity = max(m_index_capacity, m_index_count);

				switch (format)
				{
				case IndexFormat::UInt16: index_stride = sizeof(int16_t); break;
				case IndexFormat::UInt32: index_stride = sizeof(int32_t); break;
				}

				if (m_index_capacity > 0 && indices)
				{
					// release existing buffer
					if (index_buffer)
						index_buffer->Release();
					index_buffer = nullptr;
				
					// buffer description
					D3D11_BUFFER_DESC desc = { 0 };
					desc.ByteWidth = index_stride * m_index_capacity;
					desc.Usage = D3D11_USAGE_DYNAMIC;
					desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
					desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

					// buffer data
					D3D11_SUBRESOURCE_DATA data = { 0 };
					data.pSysMem = indices;

					// create
					state.device->CreateBuffer(&desc, &data, &index_buffer);
				}
			}
			else if (indices)
			{
				D3D11_MAPPED_SUBRESOURCE resource;
				state.context->Map(index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
				memcpy(resource.pData, indices, index_stride * count);
				state.context->Unmap(index_buffer, 0);
			}
		}

		virtual void vertex_data(const VertexFormat& format, const void* vertices, int64_t count) override
		{
			m_vertex_count = count;

			// recreate buffer if we've changed
			if (vertex_format.stride != format.stride || !vertex_buffer || m_vertex_count > m_vertex_capacity)
			{
				m_vertex_capacity = max(m_vertex_capacity, m_vertex_count);
				vertex_format = format;

				// discard old buffer
				if (vertex_buffer)
					vertex_buffer->Release();
				vertex_buffer = nullptr;

				if (m_vertex_capacity > 0 && vertices)
				{
					// buffer description
					D3D11_BUFFER_DESC desc = { 0 };
					desc.ByteWidth = format.stride * m_vertex_capacity;
					desc.Usage = D3D11_USAGE_DYNAMIC;
					desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
					desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

					// buffer data
					D3D11_SUBRESOURCE_DATA data = { 0 };
					data.pSysMem = vertices;

					// create
					state.device->CreateBuffer(&desc, &data, &vertex_buffer);
				}
			}
			// otherwise just update it
			else if (vertices)
			{
				D3D11_MAPPED_SUBRESOURCE resource;
				state.context->Map(vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource);
				memcpy(resource.pData, vertices, vertex_format.stride * count);
				state.context->Unmap(vertex_buffer, 0);
			}
		}

		virtual void instance_data(const VertexFormat& format, const void* instances, int64_t count) override
		{

		}

		virtual int64_t index_count() const override
		{
			return m_index_count;
		}

		virtual int64_t vertex_count() const override
		{
			return m_vertex_count;
		}

		virtual int64_t instance_count() const override
		{
			return 0;
		}
	};

	bool GraphicsBackend::init()
	{
		state = D3D11();
		state.last_size = Point(App::draw_width(), App::draw_height());

		DXGI_SWAP_CHAIN_DESC desc = { 0 };
		desc.BufferDesc.RefreshRate.Numerator = 0;
		desc.BufferDesc.RefreshRate.Denominator = 1;
		desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 2;
		desc.OutputWindow = (HWND)PlatformBackend::d3d11_get_hwnd();
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.Windowed = true;

		D3D_FEATURE_LEVEL feature_level;
		UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_DEBUG;
		HRESULT hr = D3D11CreateDeviceAndSwapChain(
			NULL,
			D3D_DRIVER_TYPE_HARDWARE,
			NULL,
			flags,
			NULL,
			0,
			D3D11_SDK_VERSION,
			&desc,
			&state.swap_chain,
			&state.device,
			&feature_level,
			&state.context);

		if (hr != S_OK || !state.swap_chain || !state.device || !state.context)
			return false;

		// get the backbuffer
		ID3D11Texture2D* frame_buffer = nullptr;
		state.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&frame_buffer);
		if (frame_buffer)
		{
			state.device->CreateRenderTargetView(frame_buffer, nullptr, &state.backbuffer);
			frame_buffer->Release();
		}

		state.features.instancing = true;
		state.features.max_texture_size = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
		state.features.origin_bottom_left = false;

		return true;
	}

	Renderer GraphicsBackend::renderer()
	{
		return Renderer::D3D11;
	}

	void GraphicsBackend::shutdown()
	{
		state.swap_chain->Release();
		state.context->Release();
		state.device->Release();
	}

	const RendererFeatures& GraphicsBackend::features()
	{
		return state.features;
	}

	void GraphicsBackend::frame()
	{
	}

	void GraphicsBackend::before_render()
	{
		auto next_size = Point(App::draw_width(), App::draw_height());
		if (state.last_size != next_size)
		{
			// release old buffer
			if (state.backbuffer)
				state.backbuffer->Release();

			// perform resize
			state.swap_chain->ResizeBuffers(2, next_size.x, next_size.y, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
			state.last_size = next_size;

			// get the new buffer
			ID3D11Texture2D* frame_buffer = nullptr;
			state.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&frame_buffer);
			if (frame_buffer)
			{
				state.device->CreateRenderTargetView(frame_buffer, nullptr, &state.backbuffer);
				frame_buffer->Release();
			}
		}
	}

	void GraphicsBackend::after_render()
	{
		state.swap_chain->Present(1, 0);
	}

	TextureRef GraphicsBackend::create_texture(int width, int height, TextureFilter filter, TextureWrap wrap_x, TextureWrap wrap_y, TextureFormat format)
	{
		auto result = new D3D11_Texture(width, height, filter, wrap_x, wrap_y, format, false);
		if (result->texture)
			return TextureRef(result);

		delete result;
		return TextureRef();
	}

	FrameBufferRef GraphicsBackend::create_framebuffer(int width, int height, const TextureFormat* attachments, int attachmentCount)
	{
		return FrameBufferRef(new D3D11_FrameBuffer(width, height, attachments, attachmentCount));
	}

	ShaderRef GraphicsBackend::create_shader(const ShaderData* data)
	{
		auto result = new D3D11_Shader(data);
		if (result->vertex && result->fragment && result->vertex_blob && result->fragment_blob)
			return ShaderRef(result);

		delete result;
		return ShaderRef();
	}

	MeshRef GraphicsBackend::create_mesh()
	{
		return MeshRef(new D3D11_Mesh());
	}

	void apply_uniforms(D3D11_Shader* shader, const MaterialRef& material, ShaderType type)
	{
		// HACK:
		// Apply Uniforms ... I don't like how this is set up at all! This needs to be better!!
		// The fact it builds this every render call is UGLY

		auto& buffers = (type == ShaderType::Vertex ? shader->vcb : shader->fcb);

		Vector<float> buffer_input;
		for (int i = 0; i < buffers.size(); i++)
		{
			// build block
			const float* data = material->data();
			for (auto& it : shader->uniforms())
			{
				if (it.type == UniformType::Texture)
					continue;

				int size = 0;
				switch (it.type)
				{
				case UniformType::Float: size = 1; break;
				case UniformType::Float2: size = 2; break;
				case UniformType::Float3: size = 3; break;
				case UniformType::Float4: size = 4; break;
				case UniformType::Mat3x2: size = 6; break;
				case UniformType::Mat4x4: size = 16; break;
				}

				int length = size * it.array_length;

				if (it.buffer_index == i && ((int)it.shader & (int)type) != 0)
				{
					auto start = buffer_input.expand(length);
					memcpy(start, data, sizeof(float) * length);
				}

				data += length;
			}

			// apply block
			if (buffers[i])
			{
				D3D11_MAPPED_SUBRESOURCE map;
				state.context->Map(buffers[i], 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
				memcpy(map.pData, buffer_input.begin(), buffer_input.size() * sizeof(float));
				state.context->Unmap(buffers[i], 0);
			}

			// clear for next buffer
			buffer_input.clear();
		}
	}

	void GraphicsBackend::render(const RenderPass& pass)
	{
		auto ctx = state.context;
		auto mesh = (D3D11_Mesh*)pass.mesh.get();
		auto shader = (D3D11_Shader*)(pass.material->shader().get());

		// OM
		{
			// Set the Target
			if (pass.target == App::backbuffer || !pass.target)
			{
				ctx->OMSetRenderTargets(1, &state.backbuffer, nullptr);
			}
			else
			{
				auto target = (D3D11_FrameBuffer*)(pass.target.get());
				ctx->OMSetRenderTargets(target->color_views.size(), target->color_views.begin(), target->depth_view);
			}

			// Depth
			// TODO: Doesn't actually assign proper values
			// TODO: Cache this
			{
				if (state.depth_state)
					state.depth_state->Release();

				D3D11_DEPTH_STENCIL_DESC desc = {};
				desc.DepthEnable = FALSE;
				desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
				desc.DepthFunc = D3D11_COMPARISON_NEVER;

				state.device->CreateDepthStencilState(&desc, &state.depth_state);
				ctx->OMSetDepthStencilState(state.depth_state, 0);
			}

			// Blend Mode
			{
				auto blend = state.get_blend(pass.blend);
				auto color = Color::from_rgba(pass.blend.rgba);
				float factor[4]{ color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
				auto mask = 0xffffffff;
				ctx->OMSetBlendState(blend, factor, mask);
			}
		}

		// IA
		{
			// We draw triangles
			ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// Assign Layout
			auto layout = state.get_layout(shader, mesh->vertex_format);
			ctx->IASetInputLayout(layout);

			// Assign Vertex Buffer
			{
				UINT stride = mesh->vertex_format.stride;
				UINT offset = 0;

				ctx->IASetVertexBuffers(
					0,
					1,
					&mesh->vertex_buffer,
					&stride,
					&offset);

				D3D11_BUFFER_DESC desc;
				mesh->vertex_buffer->GetDesc(&desc);
			}

			// Assign Index Buffer
			{
				DXGI_FORMAT format = DXGI_FORMAT_R16_UINT;

				switch (mesh->index_format)
				{
				case IndexFormat::UInt16: format = DXGI_FORMAT_R16_UINT; break;
				case IndexFormat::UInt32: format = DXGI_FORMAT_R32_UINT; break;
				}

				ctx->IASetIndexBuffer(mesh->index_buffer, format, 0);
			}
		}

		// VS
		{
			apply_uniforms(shader, pass.material, ShaderType::Vertex);
			ctx->VSSetShader(shader->vertex, nullptr, 0);
			ctx->VSSetConstantBuffers(0, shader->vcb.size(), shader->vcb.begin());
		}

		// PS
		{
			apply_uniforms(shader, pass.material, ShaderType::Fragment);
			ctx->PSSetShader(shader->fragment, nullptr, 0);
			ctx->PSSetConstantBuffers(0, shader->fcb.size(), shader->fcb.begin());

			// Fragment Shader Textures
			auto& textures = pass.material->textures();
			for (int i = 0; i < textures.size(); i++)
			{
				if (textures[i])
				{
					auto view = ((D3D11_Texture*)textures[i].get())->view;
					ctx->PSSetShaderResources(i, 1, &view);
				}
			}

			// Sampler
			// TODO: Doesn't actually assign proper values
			// TODO: Cache this
			{
				if (state.sampler)
					state.sampler->Release();

				D3D11_SAMPLER_DESC samplerDesc = {};
				samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
				samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
				samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
				samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
				samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

				state.device->CreateSamplerState(&samplerDesc, &state.sampler);
				ctx->PSSetSamplers(0, 1, &state.sampler);
			}
		}

		// RS
		{
			// Set the Viewport
			{
				D3D11_VIEWPORT viewport = {
					pass.viewport.x, pass.viewport.y,
					pass.viewport.w, pass.viewport.h,
					0.0f, 1.0f };

				ctx->RSSetViewports(1, &viewport);
			}

			// Scissor Rect
			if (pass.has_scissor)
			{
				D3D11_RECT scissor = {
					pass.scissor.x, pass.scissor.y,
					pass.scissor.x + pass.scissor.w, pass.scissor.y + pass.scissor.h };

				ctx->RSSetScissorRects(1, &scissor);
			}
			else
			{
				ctx->RSSetScissorRects(0, nullptr);
			}

			// Rasterizer
			// TODO: Doesn't actually assign proper values
			// TODO: Cache this
			{
				if (state.rasterizer)
					state.rasterizer->Release();

				D3D11_RASTERIZER_DESC rasterizerDesc = {};
				rasterizerDesc.FillMode = D3D11_FILL_SOLID;
				rasterizerDesc.CullMode = D3D11_CULL_NONE;
				rasterizerDesc.FrontCounterClockwise = true;
				rasterizerDesc.DepthBias = 0;
				rasterizerDesc.DepthBiasClamp = 0;
				rasterizerDesc.SlopeScaledDepthBias = 0;
				rasterizerDesc.DepthClipEnable = false;
				rasterizerDesc.ScissorEnable = pass.has_scissor;
				rasterizerDesc.MultisampleEnable = false;
				rasterizerDesc.AntialiasedLineEnable = false;

				state.device->CreateRasterizerState(&rasterizerDesc, &state.rasterizer);
				ctx->RSSetState(state.rasterizer);
			}
		}

		// Draw
		{
			if (mesh->instance_count() <= 0)
			{
				ctx->DrawIndexed(pass.index_count, pass.index_start, 0);
			}
			else
			{
				// TODO:
				// Draw Instanced data
				BLAH_ASSERT(false, "Instanced Drawing not implemented yet");
			}
		}

		// UnBind Shader Resources
		{
			auto& textures = pass.material->textures();
			ID3D11ShaderResourceView* view = nullptr;
			for (int i = 0; i < textures.size(); i++)
				ctx->PSSetShaderResources(i, 1, &view);
		}
	}

	void GraphicsBackend::clear_backbuffer(Color color)
	{
		float clear[4] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
		state.context->ClearRenderTargetView(state.backbuffer, clear);
	}

	ID3D11InputLayout* D3D11::get_layout(D3D11_Shader* shader, const VertexFormat& format)
	{
		// find existing
		for (auto& it : layout_cache)
		{
			if (it.shader_hash == shader->hash && it.format.stride == format.stride && it.format.attributes.size() == format.attributes.size())
			{
				bool same_format = true;
				for (int n = 0; same_format && n < format.attributes.size(); n++)
					if (it.format.attributes[n].index != format.attributes[n].index || 
						it.format.attributes[n].type != format.attributes[n].type ||
						it.format.attributes[n].normalized != format.attributes[n].normalized)
						same_format = false;

				if (same_format)
					return it.layout;
			}
		}

		// create a new one
		Vector<D3D11_INPUT_ELEMENT_DESC> desc;
		for (int i = 0; i < shader->attributes.size(); i++)
		{
			auto it = desc.expand();
			it->SemanticName = shader->attributes[i].semantic_name;
			it->SemanticIndex = shader->attributes[i].semantic_index;

			if (!format.attributes[i].normalized)
			{
				switch (format.attributes[i].type)
				{
				case VertexType::Float: it->Format = DXGI_FORMAT_R32_FLOAT;  break;
				case VertexType::Float2: it->Format = DXGI_FORMAT_R32G32_FLOAT; break;
				case VertexType::Float3: it->Format = DXGI_FORMAT_R32G32B32_FLOAT; break;
				case VertexType::Float4: it->Format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
				case VertexType::Byte4: it->Format = DXGI_FORMAT_R8G8B8A8_SINT; break;
				case VertexType::UByte4: it->Format = DXGI_FORMAT_R8G8B8A8_UINT; break;
				case VertexType::Short2: it->Format = DXGI_FORMAT_R16G16_SINT; break;
				case VertexType::UShort2: it->Format = DXGI_FORMAT_R16G16_UINT; break;
				case VertexType::Short4: it->Format = DXGI_FORMAT_R16G16B16A16_SINT; break;
				case VertexType::UShort4: it->Format = DXGI_FORMAT_R16G16B16A16_UINT; break;
				}
			}
			else
			{
				switch (format.attributes[i].type)
				{
				case VertexType::Float: it->Format = DXGI_FORMAT_R32_FLOAT;  break;
				case VertexType::Float2: it->Format = DXGI_FORMAT_R32G32_FLOAT; break;
				case VertexType::Float3: it->Format = DXGI_FORMAT_R32G32B32_FLOAT; break;
				case VertexType::Float4: it->Format = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
				case VertexType::Byte4: it->Format = DXGI_FORMAT_R8G8B8A8_SNORM; break;
				case VertexType::UByte4: it->Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
				case VertexType::Short2: it->Format = DXGI_FORMAT_R16G16_SNORM; break;
				case VertexType::UShort2: it->Format = DXGI_FORMAT_R16G16_UNORM; break;
				case VertexType::Short4: it->Format = DXGI_FORMAT_R16G16B16A16_SNORM; break;
				case VertexType::UShort4: it->Format = DXGI_FORMAT_R16G16B16A16_UNORM; break;
				}
			}

			it->InputSlot = 0;
			it->AlignedByteOffset = (i == 0 ? 0 : D3D11_APPEND_ALIGNED_ELEMENT);
			it->InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
			it->InstanceDataStepRate = 0;
		}

		ID3D11InputLayout* layout = nullptr;

		auto hr = device->CreateInputLayout(
			desc.begin(),
			desc.size(),
			shader->vertex_blob->GetBufferPointer(),
			shader->vertex_blob->GetBufferSize(),
			&layout);

		if (SUCCEEDED(hr))
		{
			auto entry = layout_cache.expand();
			entry->shader_hash = shader->hash;
			entry->format = format;
			entry->layout = layout;
			return layout;
		}

		return nullptr;
	}

	ID3D11BlendState* D3D11::get_blend(const BlendMode& blend)
	{
		for (auto& it : blend_cache)
			if (it.blend == blend)
				return it.state;

		D3D11_BLEND_DESC desc = { 0 };
		desc.AlphaToCoverageEnable = 0;
		desc.IndependentBlendEnable = 0;

		desc.RenderTarget[0].BlendEnable = !(
			blend.color_src == BlendFactor::One && blend.color_dst == BlendFactor::Zero &&
			blend.alpha_src == BlendFactor::One && blend.alpha_dst == BlendFactor::Zero
			);

		desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		if (desc.RenderTarget[0].BlendEnable)
		{
			desc.RenderTarget[0].BlendOp = blend_op(blend.color_op);
			desc.RenderTarget[0].SrcBlend = blend_factor(blend.color_src);
			desc.RenderTarget[0].DestBlend = blend_factor(blend.color_dst);

			desc.RenderTarget[0].BlendOpAlpha = blend_op(blend.alpha_op);
			desc.RenderTarget[0].SrcBlendAlpha = blend_factor(blend.alpha_src);
			desc.RenderTarget[0].DestBlendAlpha = blend_factor(blend.alpha_dst);
		}

		for (int i = 1; i < 8; i ++)
			desc.RenderTarget[i] = desc.RenderTarget[0];

		ID3D11BlendState* blend_state = nullptr;
		auto hr = state.device->CreateBlendState(&desc, &blend_state);

		if (SUCCEEDED(hr))
		{
			auto entry = blend_cache.expand();
			entry->blend = blend;
			entry->state = blend_state;
			return blend_state;
		}

		return nullptr;
	}
}

#endif // BLAH_USE_D3D11