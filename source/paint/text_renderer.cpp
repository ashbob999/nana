
#include "../detail/platform_spec_selector.hpp"
#include <nana/paint/text_renderer.hpp>
#include <nana/unicode_bidi.hpp>
#include <nana/paint/detail/native_paint_interface.hpp>

namespace nana
{
	namespace paint
	{
		namespace helper
		{
			template<typename F>
			void for_each_line(const wchar_t * str, std::size_t len, int top, F & f)
			{
				auto const end = str + len;
				for(auto i = str; i != end; ++i)
				{
					if('\n' == *i)
					{
						top += static_cast<int>(f(top, str, i - str));
						str = i + 1;
					}
				}
				if(str != end)
					f(top, str, end - str);
			}


			class string_drawer
			{
			public:
				string_drawer(graphics& graph, int left, int right, align ta, bool use_ellipsis):
					graph_(graph),
					left_(left),
					right_(right),
					text_align_(ta)
				{
					if (use_ellipsis)
					{
#ifdef _nana_std_has_string_view
						ellipsis_px_ = graph.text_extent_size(std::string_view{ "...", 3 }).width;
#else
						ellipsis_px_ = graph.text_extent_size("...", 3).width;
#endif
					}
				}

				unsigned operator()(const int top, const wchar_t * buf, std::size_t bufsize)
				{
					auto const drawable = graph_.handle();
					auto const reordered = unicode_reorder(buf, bufsize);
					
					unsigned return_max_height = 0;
					unsigned string_px = 0;
					std::vector<nana::size> word_metrics;
					for (auto & ent : reordered)
					{
						auto word_sz = detail::text_extent_size(drawable, ent.begin, ent.end - ent.begin);
						word_metrics.push_back(word_sz);

						string_px += word_sz.width;
						if (word_sz.height > return_max_height)
							return_max_height = word_sz.height;
					}

					auto text_align = text_align_;
					// Checks if ellipsis is enabled and the total pixels of string is larger than the space.
					if (ellipsis_px_ && (string_px > static_cast<int>(right_ - left_)))
					{
						//The string should be drawn from left most point no matter what text align is.
						text_align = align::left;
					}

					nana::point pos{ left_, top };

					auto wdm = word_metrics.data();
					switch (text_align)
					{
					case align::left:
						for (auto & ent : reordered)
						{
							if (pos.x + static_cast<int>(wdm->width) > 0)
							{
								if (pos.x + static_cast<int>(wdm->width) <= right_ - static_cast<int>(ellipsis_px_))
								{
									//This word can be fully painted.
									detail::draw_string(drawable, pos, ent.begin, ent.end - ent.begin);
								}
								else
								{
									//This word is painted partially. Firstly, paints the word on a dummy graphics buffer.

									nana::rectangle r{ nana::size{ static_cast<unsigned>(right_ - ellipsis_px_) - pos.x, wdm->height } };

									nana::paint::graphics dummy({ r.width, r.height });
									dummy.typeface(graph_.typeface());

									dummy.bitblt(r, graph_, pos);

#ifdef _nana_std_has_string_view
									dummy.string({}, { ent.begin, ent.end - ent.begin }, graph_.palette(true));
#else
									dummy.palette(true, graph_.palette(true));
									dummy.string({}, ent.begin, ent.end - ent.begin);
#endif
									r.x = pos.x;
									r.y = top;
									graph_.bitblt(r, dummy);
									if (ellipsis_px_)
										detail::draw_string(drawable, point{ right_ - static_cast<int>(ellipsis_px_), top }, L"...", 3);
									break;
								}
							}

							pos.x += static_cast<int>(wdm->width);
							if (pos.x > right_ - static_cast<int>(ellipsis_px_))
								break;

							++wdm;
						}
						break;
					case align::center:
						pos.x = (right_ - left_ - string_px) / 2;
						for (auto & ent : reordered)
						{
							detail::draw_string(drawable, pos, ent.begin, ent.end - ent.begin);
							pos.x += (wdm++)->width;
						}
						break;
					case align::right:
						wdm = word_metrics.data() + word_metrics.size() - 1;
						pos.x = right_;
						for (auto i = reordered.crbegin(); i != reordered.crend(); ++i)
						{
							pos.x -= (wdm--)->width;
							detail::draw_string(drawable, pos, i->begin, i->end - i->begin);
						}
						break;
					}
					return return_max_height;
				}
			private:
				graphics&	graph_;
				const int	left_, right_;	//the range of rendering area in x-axis
				const align	text_align_;
				unsigned	ellipsis_px_{ 0 };
			};


			struct draw_string_auto_changing_lines
			{
				graphics & graph;
				int x, endpos;
				align text_align;

				draw_string_auto_changing_lines(graphics& graph, int x, int endpos, align ta)
					: graph(graph), x(x), endpos(endpos), text_align(ta)
				{}

				unsigned operator()(const int top, const wchar_t * buf, std::size_t bufsize)
				{
					unsigned pixels = 0;

					auto const dw = graph.handle();
					unsigned str_w = 0;

					std::vector<nana::size> ts_keeper;

					auto const reordered = unicode_reorder(buf, bufsize);

					for(auto & i : reordered)
					{
						nana::size ts = detail::text_extent_size(dw, i.begin, i.end - i.begin);
						if(ts.height > pixels) pixels = ts.height;
						ts_keeper.emplace_back(ts);
						str_w += ts.width;
					}

					//Test whether the text needs the new line.
					if(x + static_cast<int>(str_w) > endpos)
					{
						pixels = 0;
						unsigned line_pixels = 0;
						nana::point pos{ x, top };
						int orig_top = top;
						auto i_ts_keeper = ts_keeper.cbegin();
						for(auto & i : reordered)
						{
							if(line_pixels < i_ts_keeper->height)	
								line_pixels = i_ts_keeper->height;

							bool beyond_edge = (pos.x + static_cast<int>(i_ts_keeper->width) > endpos);
							if(beyond_edge)
							{
								const std::size_t len = i.end - i.begin;
								if(len > 1)
								{
									//Find the char that should be splitted
#ifdef _nana_std_has_string_view
									auto pixel_buf = graph.glyph_pixels({ i.begin, len });
#else
									std::unique_ptr<unsigned[]> pixel_buf(new unsigned[len]);
									graph.glyph_pixels(i.begin, len, pixel_buf.get());
#endif

									std::size_t idx_head = 0, idx_splitted;

									do
									{
										auto pxbuf = pixel_buf.get();

										idx_splitted = find_splitted(idx_head, len, pos.x, endpos, pxbuf);
										if(idx_splitted == len)
										{
											detail::draw_string(dw, pos, i.begin + idx_head, idx_splitted - idx_head);

											for(std::size_t i = idx_head; i < len; ++i)
												pos.x += static_cast<int>(pxbuf[i]);

											break;
										}
										//Check the word whether it is splittable.
										if(splittable(i.begin, idx_splitted))
										{
											detail::draw_string(dw, pos, i.begin + idx_head, idx_splitted - idx_head);
											idx_head = idx_splitted;
											pos.x = x;
											pos.y += static_cast<int>(line_pixels);
											line_pixels = i_ts_keeper->height;
										}
										else
										{
											//Search the splittable character from idx_head to idx_splitted
											const wchar_t * u = i.begin + idx_splitted;
											const wchar_t * head = i.begin + idx_head;

											for(; head < u; --u)
											{
												if(splittable(head, u - head))
													break;
											}

											if(u != head)
											{
												detail::draw_string(dw, pos, head, u - head);
												idx_head += u - head;
												pos.x = x;
												pos.y += static_cast<int>(line_pixels);
												line_pixels = i_ts_keeper->height;
											}
											else
											{
												u = i.begin + idx_splitted;
												const wchar_t * end = i.begin + len;
												for(; u < end; ++u)
												{
													if(splittable(head, u - head))
														break;
												}
												std::size_t splen = u - head;
												pos.y += static_cast<int>(line_pixels);
												pos.x = x;
												detail::draw_string(dw, pos, head, splen);
												line_pixels = i_ts_keeper->height;

												for(std::size_t k = idx_head; k < idx_head + splen; ++k)
													pos.x += static_cast<int>(pxbuf[k]);

												if (pos.x >= endpos)
												{
													pos.x = x;
													pos.y += static_cast<int>(line_pixels);
												}
												idx_head += splen;
											}
										}
									}while(idx_head < len);
								}
								else
								{
									pos.x = x;
									pos.y += static_cast<int>(line_pixels);
									detail::draw_string(dw, pos, i.begin, 1);
									pos.x += static_cast<int>(i_ts_keeper->width);
								}
								line_pixels = 0;
							}
							else
							{
								detail::draw_string(dw, pos, i.begin, i.end - i.begin);
								pos.x += static_cast<int>(i_ts_keeper->width);
							}

							++i_ts_keeper;
						}

						pixels = (top - orig_top) + line_pixels;
					}
					else
					{
						//The text could be drawn in a line.
						if((align::left == text_align) || (align::center == text_align))
						{
							point pos{ x, top };
							if(align::center == text_align)
								pos.x += (endpos - x - static_cast<int>(str_w)) / 2;
							auto i_ts_keeper = ts_keeper.cbegin();
							for(auto & ent : reordered)
							{
								const nana::size & ts = *i_ts_keeper;

								if (pos.x + static_cast<int>(ts.width) > 0)
									detail::draw_string(dw, pos, ent.begin, ent.end - ent.begin);

								pos.x += static_cast<int>(ts.width);
								++i_ts_keeper;
							}					
						}
						else if(align::right == text_align)
						{
							point pos{ endpos, top };
							auto i_ts_keeper = ts_keeper.crbegin();
							for(auto i = reordered.crbegin(); i != reordered.crend(); ++i)
							{
								auto & ent = *i;
								std::size_t len = ent.end - ent.begin;
								const nana::size & ts = *i_ts_keeper;

								pos.x -= static_cast<int>(ts.width);
								if (pos.x >= 0)
									detail::draw_string(dw, pos, ent.begin, len);
								++i_ts_keeper;
							}							
						}
					}
					return pixels;
				}

				static std::size_t find_splitted(std::size_t begin, std::size_t end, int x, int endpos, unsigned * pxbuf)
				{
					for (std::size_t i = begin; i < end; ++i)
					{
						if ((x += static_cast<int>(pxbuf[i])) > endpos)
							return (begin == i ? i + 1 : i);
					}
					return end;
				}

				static bool splittable(const wchar_t * str, std::size_t index)
				{
					wchar_t ch = str[index];
					if(('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z'))
					{
						if(index)
						{
							auto prch = str[index - 1];
							if('0' <= ch && ch <= '9')
								return !(('0' <= prch && prch <= '9') || (prch == '-'));

							return (('z' < prch || prch < 'a') && ('Z' < prch || prch < 'A'));
						}
						else
							return false;
					}
					return true;
				}
			};

			struct extent_auto_changing_lines
			{
				graphics & graph;
				int x, endpos;
				unsigned extents;

				extent_auto_changing_lines(graphics& graph, int x, int endpos)
					: graph(graph), x(x), endpos(endpos), extents(0)
				{}

				unsigned operator()(int top, const wchar_t * buf, std::size_t bufsize)
				{
					unsigned pixels = 0;

					drawable_type dw = graph.handle();
					unsigned str_w = 0;
					std::vector<nana::size> ts_keeper;

					auto const reordered = unicode_reorder(buf, bufsize);

					for(auto & i : reordered)
					{
						nana::size ts = detail::text_extent_size(dw, i.begin, i.end - i.begin);
						ts_keeper.emplace_back(ts);
						str_w += ts.width;
					}

					auto i_ts_keeper = ts_keeper.cbegin();
					//Test whether the text needs the new line.
					if(x + static_cast<int>(str_w) > endpos)
					{
						unsigned line_pixels = 0;
						int xpos = x;
						int orig_top = top;

						for(auto & i : reordered)
						{
							if(line_pixels < i_ts_keeper->height)
								line_pixels = i_ts_keeper->height;

							bool beyond_edge = (xpos + static_cast<int>(i_ts_keeper->width) > endpos);
							if(beyond_edge)
							{
								std::size_t len = i.end - i.begin;
								if(len > 1)
								{
									//Find the char that should be splitted
#ifdef _nana_std_has_string_view
									auto scope_res = graph.glyph_pixels({ i.begin, len });
									auto pxbuf = scope_res.get();
#else
									std::unique_ptr<unsigned[]> scope_res(new unsigned[len]);
									auto pxbuf = scope_res.get();
									graph.glyph_pixels(i.begin, len, pxbuf);
#endif

									std::size_t idx_head = 0, idx_splitted;

									do
									{
										idx_splitted = draw_string_auto_changing_lines::find_splitted(idx_head, len, xpos, endpos, pxbuf);

										if(idx_splitted == len)
										{
											for(std::size_t i = idx_head; i < len; ++i)
												xpos += static_cast<int>(pxbuf[i]);

											break;
										}
										//Check the word whether it is splittable.
										if(draw_string_auto_changing_lines::splittable(i.begin, idx_splitted))
										{
											idx_head = idx_splitted;
											xpos = x;
											top += line_pixels;
											line_pixels = i_ts_keeper->height;
										}
										else
										{
											//Search the splittable character from idx_head to idx_splitted
											const wchar_t * u = i.begin + idx_splitted;
											const wchar_t * head = i.begin + idx_head;

											for(; head < u; --u)
											{
												if(draw_string_auto_changing_lines::splittable(head, u - head))
													break;
											}

											if(u != head)
											{
												idx_head += u - head;
												xpos = x;
												top += line_pixels;
												line_pixels = i_ts_keeper->height;
											}
											else
											{
												u = i.begin + idx_splitted;
												const wchar_t * end = i.begin + len;
												for(; u < end; ++u)
												{
													if(draw_string_auto_changing_lines::splittable(head, u - head))
														break;
												}
												std::size_t splen = u - head;
												top += line_pixels;
												xpos = x;
												line_pixels = i_ts_keeper->height;

												for(std::size_t k = idx_head; k < idx_head + splen; ++k)
													xpos += static_cast<int>(pxbuf[k]);
												if(xpos >= endpos)
												{
													xpos = x;
													top += line_pixels;
												}
												idx_head += splen;
											}
										}
									}while(idx_head < len);
								}
								else
									xpos = x + static_cast<int>(i_ts_keeper->width);

								line_pixels = 0;
							}
							else
								xpos += static_cast<int>(i_ts_keeper->width);

							++i_ts_keeper;
						}

						pixels = (top - orig_top) + line_pixels;
					}
					else
					{
						while(i_ts_keeper != ts_keeper.cend())
						{
							const nana::size & ts = *(i_ts_keeper++);
							if(ts.height > pixels) pixels = ts.height;
						}
					}
					extents += pixels;
					return pixels;
				}
			};
		}//end namespace helper

		//class text_renderer
		text_renderer::text_renderer(graph_reference graph, align ta)
			: graph_(graph), text_align_(ta)
		{}

		nana::size text_renderer::extent_size(int x, int y, const wchar_t* str, std::size_t len, unsigned restricted_pixels) const
		{
			nana::size extents;
			if(graph_)
			{
				helper::extent_auto_changing_lines eacl(graph_, x, x + static_cast<int>(restricted_pixels));
				helper::for_each_line(str, len, y, eacl);
				extents.width = restricted_pixels;
				extents.height = eacl.extents;
			}
			return extents;
		}

		void text_renderer::render(const point& pos, const wchar_t * str, std::size_t len)
		{
			if (graph_)
			{
				helper::string_drawer sd{ graph_, pos.x, static_cast<int>(graph_.width()), text_align_, false };
				helper::for_each_line(str, len, pos.y, sd);
			}
		}

		void text_renderer::render(const point& pos, const wchar_t* str, std::size_t len, unsigned restricted_pixels, bool omitted)
		{
			if (graph_)
			{
				helper::string_drawer sd{ graph_, pos.x, pos.x + static_cast<int>(restricted_pixels), text_align_, omitted };
				helper::for_each_line(str, len, pos.y, sd);
			}
		}

		void text_renderer::render(const point& pos, const wchar_t * str, std::size_t len, unsigned restricted_pixels)
		{
			if (graph_)
			{
				helper::draw_string_auto_changing_lines dsacl(graph_, pos.x, pos.x + static_cast<int>(restricted_pixels), text_align_);
				helper::for_each_line(str, len, pos.y, dsacl);
			}
		}
		//end class text_renderer

		//class aligner

		//Constructor
		aligner::aligner(graph_reference graph, align text_align)
			: aligner{ graph, text_align, text_align }
		{}

		aligner::aligner(graph_reference graph, align text_align, align text_align_ex) :
			graph_(graph),
			text_align_(text_align), 
			text_align_ex_(text_align_ex)
		{}

		// Draws a text with specified text alignment.
		void aligner::draw(const std::string& text, point pos, unsigned width)
		{
			draw(to_wstring(text), pos, width);
		}

		void aligner::draw(const std::wstring& text, point pos, unsigned width)
		{
			auto text_px = graph_.text_extent_size(text).width;
			if (text_px <= width)
			{
				switch (text_align_)
				{
				case align::center:
					pos.x += static_cast<int>(width - text_px) / 2;
					break;
				case align::right:
					pos.x += static_cast<int>(width - text_px);
				default:
					break;
				}

#ifdef _nana_std_has_string_view
				graph_.bidi_string(pos, text);
#else
				graph_.bidi_string(pos, text.c_str(), text.size());
#endif
				return;
			}

#ifdef _nana_std_has_string_view
			const auto ellipsis = graph_.text_extent_size(std::string_view{ "...", 3 }).width;
			auto pixels = graph_.glyph_pixels({ text.c_str(), text.size() });
#else
			const auto ellipsis = graph_.text_extent_size("...", 3).width;

			std::unique_ptr<unsigned[]> pixels(new unsigned[text.size()]);
			graph_.glyph_pixels(text.c_str(), text.size(), pixels.get());
#endif

			std::size_t substr_len = 0;
			unsigned substr_px = 0;

			if (align::right == text_align_ex_)
			{
				auto end = pixels.get();
				auto p = end + text.size();
				do
				{
					--p;
					if (substr_px + *p + ellipsis > width)
					{
						substr_len = p - pixels.get() + 1;
						break;
					}
					substr_px += *p;
				} while (p != end);

				pos.x += static_cast<int>(width - ellipsis - substr_px) + ellipsis;
#ifdef _nana_std_has_string_view
				graph_.bidi_string(pos, { text.c_str() + substr_len, text.size() - substr_len });
#else
				graph_.bidi_string(pos, text.c_str() + substr_len, text.size() - substr_len);
#endif
				pos.x -= ellipsis;
			}
			else
			{
				for (auto p = pixels.get(), end = pixels.get() + text.size(); p != end; ++p)
				{
					if (substr_px + *p + ellipsis > width)
					{
						substr_len = p - pixels.get();
						break;
					}
					substr_px += *p;
				}

				if (align::center == text_align_ex_)
					pos.x += (width - substr_px - ellipsis) / 2;
#ifdef _nana_std_has_string_view
				graph_.bidi_string(pos, { text.c_str(), substr_len });
#else
				graph_.bidi_string(pos, text.c_str(), substr_len);
#endif

				pos.x += substr_px;
			}

			graph_.string(pos, "...");
		}

		//end class string
	}
}
