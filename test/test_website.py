import json
import re
import struct
import unittest
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "website"


class PageInventory(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.links = []
        self.images = []

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if "id" in values:
            self.ids.add(values["id"])
        if tag == "a" and "href" in values:
            self.links.append(values["href"])
        if tag == "img":
            self.images.append(values)


class WebsiteContractTests(unittest.TestCase):
    pages = (SITE / "index.html", SITE / "de" / "index.html")

    def read(self, path):
        return path.read_text(encoding="utf-8")

    def test_bilingual_status_is_current_and_bounded(self):
        english, german = (self.read(path) for path in self.pages)
        for text in (english, german):
            self.assertIn("R8.1k", text)
            self.assertIn("x86_64", text)
            self.assertIn("i386", text)
            self.assertNotIn("High-Assurance S0 gate", text)
            self.assertNotIn("High-Assurance-Gate S0", text)
        self.assertIn("not yet the production system", english)
        self.assertIn("noch nicht das produktive System", german)
        self.assertIn("VMware desktop and audio path", english)
        self.assertIn("VMware-Desktop und Audio", german)
        self.assertIn("dmesg", english)
        self.assertIn("dmesg", german)

    def test_local_links_images_alt_text_and_dimensions_resolve(self):
        for page in self.pages:
            parser = PageInventory()
            parser.feed(self.read(page))
            for href in parser.links:
                parsed = urlsplit(href)
                if parsed.scheme or href.startswith("/"):
                    continue
                if parsed.path:
                    self.assertTrue((page.parent / parsed.path).resolve().exists(), href)
                if parsed.fragment:
                    self.assertIn(parsed.fragment, parser.ids, href)
            self.assertGreaterEqual(len(parser.images), 8)
            for image in parser.images:
                self.assertTrue(image.get("alt", "").strip())
                path = (page.parent / image["src"]).resolve()
                self.assertTrue(path.exists(), image["src"])
                with path.open("rb") as handle:
                    self.assertEqual(handle.read(8), b"\x89PNG\r\n\x1a\n")
                    length = struct.unpack(">I", handle.read(4))[0]
                    self.assertEqual(handle.read(4), b"IHDR")
                    width, height = struct.unpack(">II", handle.read(8))
                    self.assertGreaterEqual(length, 13)
                self.assertEqual(int(image["width"]), width)
                self.assertEqual(int(image["height"]), height)

    def test_metadata_and_json_ld_are_bilingual_and_parseable(self):
        for page, language, canonical in (
            (self.pages[0], "en", "https://reist-os.intracom.at/"),
            (self.pages[1], "de", "https://reist-os.intracom.at/de/"),
        ):
            text = self.read(page)
            self.assertIn(f'<html lang="{language}">', text)
            self.assertIn(f'<link rel="canonical" href="{canonical}">', text)
            self.assertIn('hreflang="en"', text)
            self.assertIn('hreflang="de"', text)
            match = re.search(
                r'<script type="application/ld\+json">\s*(.*?)\s*</script>',
                text,
                re.DOTALL,
            )
            self.assertIsNotNone(match)
            graph = json.loads(match.group(1))["@graph"]
            self.assertTrue(any(node.get("@type") == "WebSite" for node in graph))
            self.assertTrue(any(node.get("@id", "").endswith("#software") for node in graph))


if __name__ == "__main__":
    unittest.main()
