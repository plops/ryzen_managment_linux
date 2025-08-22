some modifications to implot are needed to compile with most recent imgui:

```
Index: implot.cpp
IDEA additional info:
Subsystem: com.intellij.openapi.diff.impl.patch.CharsetEP
<+>UTF-8
===================================================================
diff --git a/implot.cpp b/implot.cpp
--- a/implot.cpp	(revision 18c72431f8265e2b0b5378a3a73d8a883b2175ff)
+++ b/implot.cpp	(date 1755893486332)
@@ -342,7 +342,7 @@
// Align to be pixel perfect
pos.x = IM_FLOOR(pos.x);
pos.y = IM_FLOOR(pos.y);
-    const float scale = g.FontSize / font->FontSize;
+    const float scale = g.FontSize / ImGui::GetFontSize(); //g.FontSize / font->FontSize;
     const char* s = text_begin;
     int chars_exp = (int)(text_end - s);
     int chars_rnd = 0;
     @@ -359,7 +359,8 @@
     if (c == 0) // Malformed UTF-8?
     break;
     }
-        const ImFontGlyph * glyph = font->FindGlyph((ImWchar)c);
+        auto fontBaked = font->GetFontBaked(ImGui::GetFontSize());
+        const ImFontGlyph * glyph = fontBaked->FindGlyph((ImWchar)c);
         if (glyph == nullptr) {
             continue;
         }
```