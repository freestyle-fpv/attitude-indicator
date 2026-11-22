import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('package_installer',Path(__file__).resolve().parents[1]/'tools/package_installer.py')
package=importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

class ServicePages(unittest.TestCase):
    def fixture(self, root, html):
        service=root/'installer';service.mkdir()
        (service/'diagnostics.html').write_text(html)
        (service/'service.css').write_text('body{color:#68f0c4}')
        (service/'diagnostics-core.js').write_text('window.exampleCore=17;')
        (service/'diagnostics.js').write_text('window.exampleApp=23;')

    def test_diagnostic_page_needs_no_unused_instrument_image(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            self.fixture(root,'<link rel="stylesheet" href="service.css"><script src="diagnostics-core.js"></script><script src="diagnostics.js"></script><a href="#install">安装</a>')
            with patch.object(package,'ROOT',root):page=package.service_page('diagnostics.html')
            self.assertIn('<style>body{color:#68f0c4}</style>',page)
            self.assertIn('<script>window.exampleCore=17;</script>',page)
            self.assertIn('<script>window.exampleApp=23;</script>',page)
            self.assertIn('href="#install"',page)
            self.assertNotIn('src=',page)

    def test_installer_image_bytes_and_diagnostic_link_survive_offline_packaging(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            self.fixture(root,'<img src="screenshots/attitude.png"><a href="#diagnostics">诊断</a>')
            image=root/'installer/screenshots/attitude.png';image.parent.mkdir(parents=True);image.write_bytes(b'PNG-data')
            with patch.object(package,'ROOT',root):page=package.service_page('diagnostics.html')
            self.assertIn('src="data:image/png;base64,UE5HLWRhdGE="',page)
            self.assertIn('href="#diagnostics"',page)

    def test_four_carousel_images_are_embedded_without_external_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            names=('attitude','hud','airspeed','altimeter')
            self.fixture(root,''.join('<img src="screenshots/'+name+'.png">' for name in names))
            folder=root/'installer/screenshots';folder.mkdir()
            for name in names:(folder/(name+'.png')).write_bytes(name.encode())
            with patch.object(package,'ROOT',root):page=package.service_page('diagnostics.html')
            self.assertEqual(page.count('src="data:image/png;base64,'),4)
            self.assertNotIn('src="screenshots/',page)

    def test_embedded_script_cannot_escape_its_html_script_element(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            self.fixture(root,'<script src="diagnostics.js"></script>')
            (root/'installer/diagnostics.js').write_text('const bad="</script><script>";')
            with patch.object(package,'ROOT',root):
                with self.assertRaisesRegex(ValueError,'closing script tag'):
                    package.service_page('diagnostics.html')

class ChineseInstaller(unittest.TestCase):
    def test_erase_confirmation_and_install_actions_are_translated_in_bundled_source(self):
        source='const titles=["Erase device","Confirm Installation","Preparing installation"];const view=html`<p>Do you want to erase the device before installing ${name}? All data on the device will be lost.</p><ewt-page-message label="Installation complete!"></ewt-page-message><button>Back</button><button>Next</button><button>Install</button>`;const wire={eraseAll:!1,compress:!0};'
        translated=package.localize_dialog(source)
        self.assertNotIn('Do you want to erase the device before installing',translated)
        self.assertNotIn('label="Installation complete!"',translated)
        self.assertNotIn('"Confirm Installation"',translated)
        self.assertIn('清除设备数据',translated)
        self.assertIn('确认安装',translated)
        self.assertIn('安装完成',translated)
        # UI localization leaves flashing option names and values untouched.
        self.assertIn('eraseAll:!1',translated)
        self.assertIn('compress:!0',translated)

if __name__=='__main__':unittest.main()
